package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"net"
	"net/http"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"slices"
	"strings"
	"syscall"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
)

// asset_add: a file the person gave the chat (a picture, a sprite sheet, a
// Tiled map, an Aseprite file, a sound, a font) saved into the open game's
// assets folder. The host hands it over as a file parameter (openai/fileParams):
// a short-lived https link jm downloads.

// assetLimit: the biggest file asset_add takes.
const assetLimit = 32 << 20

// assetKinds: the extensions a game asset may have, each with the content
// types a host may serve it as (a generic binary type is always taken).
var assetKinds = map[string][]string{
	".png": {"image/png"}, ".jpg": {"image/jpeg"}, ".jpeg": {"image/jpeg"},
	".aseprite": nil, ".ase": nil,
	".tmj": {"application/json", "text/plain"}, ".tsj": {"application/json", "text/plain"}, ".json": {"application/json", "text/plain"},
	".wav": {"audio/wav", "audio/x-wav", "audio/wave", "audio/vnd.wave"}, ".ogg": {"audio/ogg", "application/ogg"}, ".mp3": {"audio/mpeg"},
	".ttf": {"font/ttf", "application/x-font-ttf"}, ".otf": {"font/otf", "application/x-font-otf"},
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

// publicAddressOnly refuses a connection to a loopback, private, link-local
// or otherwise non-public address: checked at dial time, after DNS.
func publicAddressOnly(_, address string, _ syscall.RawConn) error {
	host, _, err := net.SplitHostPort(address)
	if err != nil {
		return err
	}
	ip := net.ParseIP(host)
	if ip == nil || ip.IsLoopback() || ip.IsPrivate() || ip.IsLinkLocalUnicast() || ip.IsLinkLocalMulticast() ||
		ip.IsUnspecified() || ip.IsMulticast() || ip.IsInterfaceLocalMulticast() {
		return fmt.Errorf("refused %s: not a public address", host)
	}
	return nil
}

func (s *mcpServer) assetTools() []mcpTool {
	home := gamesHome()
	fileSchema := map[string]any{"type": "object", "description": "the file the person attached (the host fills it in)",
		"properties": map[string]any{"download_url": strArg("its https link"), "file_id": strArg("the host's id for it"),
			"mime_type": strArg("its type"), "file_name": strArg("its name")},
		"required": []string{"download_url", "file_id"}}
	return []mcpTool{
		{Name: "asset_add", Title: "Add a file to the game", Meta: map[string]any{"openai/widgetAccessible": true, "openai/fileParams": []string{"file"},
			"openai/toolInvocation/invoking": "Adding the file…", "openai/toolInvocation/invoked": "Added"},
			Description: "Save a file the person attached (a PNG or JPEG, a sprite sheet, a Tiled .tmj/.tsj map, an .aseprite file, a .wav/.ogg/.mp3 sound, a .ttf/.otf font, JSON data) " +
				"into the open game's assets folder. path: where, from the game's folder, under assets/ (default: assets/<its name>). An existing file is kept unless replace is true. " +
				fmt.Sprintf("Up to %d MB. Answers the saved path: put it in a scene or prefab next.", assetLimit>>20),
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "idempotentHint": true, "openWorldHint": true}, // fetches from the host; replace overwrites
			InputSchema: object(map[string]any{"game": gameOfTool, "file": fileSchema,
				"path":    strArg("where to save it, under assets/, e.g. assets/sprites/hero.png"),
				"replace": boolArg("save over a file already there")}, "file"),
			OutputSchema: object(map[string]any{"path": typed("string"), "size": typed("integer"), "replaced": typed("boolean")}, "path", "size", "replaced"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				file, _ := a["file"].(map[string]any)
				link, _ := file["download_url"].(string)
				name, _ := file["file_name"].(string)
				saved, replaced, size, err := addAsset(root, link, name, a.str("path"), a.flag("replace"))
				if err != nil {
					return textResult(err.Error()+": nothing was saved", true)
				}
				return toolResult{Text: fmt.Sprintf("Saved %s (%s).", saved, sizeText(size)),
					Structured: map[string]any{"path": saved, "size": size, "replaced": replaced}}
			})},
	}
}

// addAsset downloads link into the game as target (default assets/<name>):
// the saved path, whether a file was there, and its size.
func addAsset(root, link, name, target string, replace bool) (string, bool, int64, error) {
	if target == "" {
		base := path.Base(strings.ReplaceAll(name, "\\", "/"))
		if name == "" || base == "." || base == "/" || strings.HasPrefix(base, ".") {
			return "", false, 0, errors.New("the file has no usable name: give path, e.g. assets/sprites/hero.png")
		}
		target = "assets/" + base
	}
	ext := strings.ToLower(filepath.Ext(target))
	types, known := assetKinds[ext]
	if !known {
		return "", false, 0, fmt.Errorf("%s isn't a game asset jm takes (%s)", target, assetExtensions())
	}
	if _, _, err := assetPath(root, target); err != nil { // before downloading anything
		return "", false, 0, err
	}
	data, err := downloadAsset(link, types)
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
	abs, rel, err := assetPath(root, target) // again, under the lock: the folders may have changed
	if err != nil {
		return "", false, 0, err
	}
	info, statErr := os.Lstat(abs)
	if statErr == nil && (info.IsDir() || info.Mode()&os.ModeSymlink != 0) {
		return "", false, 0, fmt.Errorf("%s is a folder or a link", rel)
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

// assetPath is gamePath for a file that must be in the game's assets folder.
func assetPath(root, target string) (string, string, error) {
	abs, rel, err := gamePath(root, target)
	if err == nil && !strings.HasPrefix(rel, "assets/") {
		err = fmt.Errorf("%s isn't in the game's assets folder", target)
	}
	return abs, rel, err
}

func downloadAsset(link string, types []string) ([]byte, error) {
	u, err := url.Parse(link)
	if err != nil || u.Scheme != "https" || u.Host == "" {
		return nil, errors.New("the file's download_url must be an https link")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 90*time.Second)
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

func assetExtensions() string {
	list := []string{}
	for ext := range assetKinds {
		list = append(list, ext)
	}
	slices.Sort(list)
	return strings.Join(list, " ")
}
