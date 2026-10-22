package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"net"
	"net/http"
	"net/netip"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"slices"
	"strings"
	"syscall"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/formats"
	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
)

// asset_add: a file the person gave the chat (a picture, a sprite sheet, a
// Tiled map, an Aseprite file, a sound, a font) saved into the open game's
// assets folder. The host hands it over as a file parameter (openai/fileParams):
// a short-lived https link jm downloads. A widget whose host takes no uploads
// sends the bytes themselves (data).

// assetLimit: the biggest file asset_add takes; inlineLimit: the biggest
// sent as data (base64, a third bigger, in one message).
const (
	assetLimit  = 32 << 20
	inlineLimit = 8 << 20
)

// uploadFormat is the format asset_add takes rel as (any case), false for none.
func uploadFormat(rel string) (formats.Format, bool) {
	f, ok := formats.Of(rel)
	return f, ok && f.Upload
}

// assetClient fetches files for asset_add: https only, never to this machine
// or its network (the link comes from the model, which may be misled).
var assetClient = &http.Client{
	Timeout:       60 * time.Second,
	CheckRedirect: httpsRedirects,
	Transport: &http.Transport{
		Proxy:               nil, // a proxy would dial for us, past the address check
		DialContext:         (&net.Dialer{Timeout: 10 * time.Second, Control: publicAddressOnly}).DialContext,
		TLSHandshakeTimeout: 10 * time.Second,
	},
}

func httpsRedirects(req *http.Request, via []*http.Request) error {
	if len(via) >= 5 {
		return errors.New("too many redirects")
	}
	if req.URL.Scheme != "https" {
		return fmt.Errorf("refused a redirect to %s: https only", req.URL.Scheme)
	}
	return nil
}

// notPublic: the special-purpose ranges (IANA's registries) inside the
// space publicAddressOnly otherwise takes: IPv4's this-network, shared
// (CGNAT, Tailscale), IETF protocol, documentation, 6to4 relay,
// benchmarking and reserved; IPv6's TEREDO and IETF protocol, documentation,
// 6to4 (an IPv4 address inside) and the newer documentation block.
var notPublic = []netip.Prefix{
	netip.MustParsePrefix("0.0.0.0/8"), netip.MustParsePrefix("100.64.0.0/10"), netip.MustParsePrefix("192.0.0.0/24"),
	netip.MustParsePrefix("192.0.2.0/24"), netip.MustParsePrefix("192.88.99.0/24"), netip.MustParsePrefix("198.18.0.0/15"),
	netip.MustParsePrefix("198.51.100.0/24"), netip.MustParsePrefix("203.0.113.0/24"), netip.MustParsePrefix("240.0.0.0/4"),
	netip.MustParsePrefix("2001::/23"), netip.MustParsePrefix("2001:db8::/32"), netip.MustParsePrefix("2002::/16"), netip.MustParsePrefix("3fff::/20"),
}

// globalIPv6: the only IPv6 space handed out for the internet (2000::/3); the rest is special.
var globalIPv6 = netip.MustParsePrefix("2000::/3")

// publicAddressOnly refuses a connection to a loopback, private, link-local
// or otherwise non-public address: checked at dial time, after DNS.
func publicAddressOnly(_, address string, _ syscall.RawConn) error {
	host, _, err := net.SplitHostPort(address)
	if err != nil {
		return err
	}
	ip, err := netip.ParseAddr(host)
	ip = ip.Unmap()
	public := err == nil && ip.Zone() == "" && ip.IsGlobalUnicast() && !ip.IsPrivate() && (ip.Is4() || globalIPv6.Contains(ip))
	for _, p := range notPublic {
		public = public && !p.Contains(ip)
	}
	if !public {
		return fmt.Errorf("refused %s: not a public address", host)
	}
	return nil
}

func (s *mcpServer) assetTools() []mcpTool {
	fileSchema := map[string]any{"type": "object", "description": "the file the person attached (the host fills it in)",
		"properties": map[string]any{"download_url": strArg("its https link"), "file_id": strArg("the host's id for it"),
			"mime_type": strArg("its type"), "file_name": strArg("its name")},
		"required": []string{"download_url", "file_id"}}
	return []mcpTool{
		{Name: "asset_add", Title: "Add a file to the game", Meta: map[string]any{"openai/widgetAccessible": true, "openai/fileParams": []string{"file"},
			"openai/toolInvocation/invoking": "Adding the file…", "openai/toolInvocation/invoked": "Added"},
			Description: "Save a file the person attached (a PNG or JPEG, a sprite sheet, a Tiled .tmj/.tsj map, an .aseprite file, a .wav/.ogg/.mp3 sound, a .ttf/.otf font, JSON data) " +
				"into the open game's assets folder. path: where, from the game's folder, under assets/ (default: assets/<its name>). An existing file is kept unless replace is true. " +
				fmt.Sprintf("Up to %d MB. Answers the saved path: put it in a scene or prefab next. ", assetLimit>>20) +
				fmt.Sprintf("data instead of file: the bytes, base64, up to %d MB, with path (Studio sends them where the host takes no uploads).", inlineLimit>>20),
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": true}, // fetches from the host; replace overwrites
			InputSchema: object(map[string]any{"file": fileSchema,
				"data":    strArg("the file's bytes, base64, instead of file (needs path)"),
				"path":    strArg("where to save it, under assets/, e.g. assets/sprites/hero.png"),
				"replace": boolArg("save over a file already there")}),
			OutputSchema: object(map[string]any{"path": typed("string"), "size": typed("integer"), "replaced": typed("boolean")}, "path", "size", "replaced"),
			onGame: func(root string, a toolArgs) toolResult {
				file, _ := a["file"].(map[string]any)
				link, _ := file["download_url"].(string)
				name, _ := file["file_name"].(string)
				fetch := func(ctx context.Context, types []string) ([]byte, error) { return downloadAsset(ctx, link, types) }
				if data := a.str("data"); data != "" || file == nil {
					if file != nil || data == "" || a.str("path") == "" {
						return textResult("give the file (attached), or its data (base64) with path: nothing was saved", true)
					}
					fetch = func(context.Context, []string) ([]byte, error) { return inlineAsset(data) }
				}
				saved, replaced, size, err := addAsset(a.call().context(), root, fetch, name, a.str("path"), a.flag("replace"))
				if err != nil {
					return textResult(err.Error()+": nothing was saved", true)
				}
				return toolResult{Text: fmt.Sprintf("Saved %s (%s).", saved, sizeText(size)),
					Structured: map[string]any{"path": saved, "size": size, "replaced": replaced}}
			}},
	}
}

// addAsset saves what fetch gives (given the content types the target's
// extension may come as) into the game as target (default assets/<name>):
// the saved path, whether a file was there, and its size. A call cancelled
// (ctx) before the write saves nothing.
func addAsset(ctx context.Context, root string, fetch func(context.Context, []string) ([]byte, error), name, target string, replace bool) (string, bool, int64, error) {
	if target == "" {
		base := path.Base(strings.ReplaceAll(name, "\\", "/"))
		if name == "" || base == "." || base == "/" || strings.HasPrefix(base, ".") {
			return "", false, 0, errors.New("the file has no usable name: give path, e.g. assets/sprites/hero.png")
		}
		target = "assets/" + base
	}
	_, rel, err := assetPath(root, target) // before downloading anything
	if err != nil {
		return "", false, 0, err
	}
	format, _ := uploadFormat(rel)
	ext := strings.ToLower(filepath.Ext(rel))
	data, err := fetch(ctx, format.MIME)
	if err != nil {
		return "", false, 0, err
	}
	if err := checkAsset(ext, data); err != nil {
		return "", false, 0, fmt.Errorf("%s: %v", target, err)
	}
	release, err := gamelock.Hold()
	if err != nil {
		return "", false, 0, err
	}
	defer release()
	if err := ctx.Err(); err != nil {
		return "", false, 0, err
	}
	abs, rel, err := assetPath(root, target) // again, under the lock: the folders may have changed
	if err != nil {
		return "", false, 0, err
	}
	info, statErr := os.Lstat(abs)
	if statErr == nil && info.IsDir() {
		return "", false, 0, fmt.Errorf("%s is a folder", rel)
	}
	if statErr == nil && !replace {
		return "", false, 0, fmt.Errorf("%s is already there: replace it, or give another path", rel)
	}
	if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
		return "", false, 0, err
	}
	if err := atomicfile.WriteFile(abs, data, 0o644); err != nil {
		return "", false, 0, err
	}
	return rel, statErr == nil, int64(len(data)), nil
}

// assetPath is gamePath for a game asset: in the assets folder, with an
// asset's extension, and not a link (whose target would be written instead).
func assetPath(root, target string) (string, string, error) {
	if filepath.IsAbs(filepath.FromSlash(target)) || strings.HasPrefix(target, "/") {
		return "", "", fmt.Errorf("%s: give the path from the game's folder, e.g. assets/sprites/hero.png", target)
	}
	if info, err := os.Lstat(filepath.Join(root, filepath.FromSlash(target))); err == nil && info.Mode()&os.ModeSymlink != 0 {
		return "", "", fmt.Errorf("%s is a link: give the file's own path", target)
	}
	abs, rel, err := gamePath(root, target)
	if err != nil {
		return "", "", err
	}
	if !strings.HasPrefix(rel, "assets/") {
		return "", "", fmt.Errorf("%s isn't in the game's assets folder", target)
	}
	if _, known := uploadFormat(rel); !known {
		return "", "", fmt.Errorf("%s isn't a game asset jm takes (%s)", rel, assetExtensions(" "))
	}
	return abs, rel, nil
}

// inlineAsset is a file sent as base64 data.
func inlineAsset(data string) ([]byte, error) {
	if base64.StdEncoding.DecodedLen(len(data)) > inlineLimit+3 {
		return nil, fmt.Errorf("the file is over %d MB: attach it to the chat instead", inlineLimit>>20)
	}
	out, err := base64.StdEncoding.DecodeString(data)
	if err != nil {
		return nil, errors.New("its data isn't base64")
	}
	if len(out) > inlineLimit {
		return nil, fmt.Errorf("the file is over %d MB: attach it to the chat instead", inlineLimit>>20)
	}
	return out, nil
}

func downloadAsset(ctx context.Context, link string, types []string) ([]byte, error) {
	u, err := url.Parse(link)
	if err != nil || u.Scheme != "https" || u.Host == "" {
		return nil, errors.New("the file's download_url must be an https link")
	}
	ctx, cancel := context.WithTimeout(ctx, 90*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u.String(), nil)
	if err != nil {
		return nil, err
	}
	resp, err := assetClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("couldn't download the file: %v", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("couldn't download the file: %s", resp.Status)
	}
	if resp.ContentLength > assetLimit {
		return nil, fmt.Errorf("the file is over %d MB", assetLimit>>20)
	}
	kind, _, _ := mime.ParseMediaType(resp.Header.Get("Content-Type"))
	if kind != "" && kind != "application/octet-stream" && kind != "binary/octet-stream" && !slices.Contains(types, kind) {
		return nil, fmt.Errorf("the file came as %s, not what its name says", kind)
	}
	data, err := io.ReadAll(io.LimitReader(resp.Body, assetLimit+1))
	if err != nil {
		return nil, fmt.Errorf("couldn't download the file: %v", err)
	}
	if len(data) > assetLimit {
		return nil, fmt.Errorf("the file is over %d MB", assetLimit>>20)
	}
	return data, nil
}

// checkAsset: the bytes are what the extension says, where that's cheap to tell.
func checkAsset(ext string, data []byte) error {
	ok := true
	switch ext {
	case ".png":
		ok = bytes.HasPrefix(data, []byte("\x89PNG\r\n\x1a\n"))
	case ".jpg", ".jpeg":
		ok = bytes.HasPrefix(data, []byte{0xff, 0xd8, 0xff})
	case ".tmj", ".tsj", ".json":
		ok = json.Valid(data)
	case ".wav":
		ok = bytes.HasPrefix(data, []byte("RIFF"))
	case ".ogg":
		ok = bytes.HasPrefix(data, []byte("OggS"))
	}
	if !ok {
		return fmt.Errorf("isn't a %s file", strings.TrimPrefix(ext, "."))
	}
	return nil
}

// assetExtensions are the endings asset_add takes, joined by sep.
func assetExtensions(sep string) string {
	list := []string{}
	for _, f := range formats.All(func(f formats.Format) bool { return f.Upload }) {
		list = append(list, f.Suffix)
	}
	slices.Sort(list)
	return strings.Join(list, sep)
}
