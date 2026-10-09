// Package toolchain finds, or fetches, what compiling scripts needs: Node.js
// and the AssemblyScript compiler. A machine's own Node (20+) is used when it
// has one; otherwise a pinned Node is downloaded, checked against the sha256
// below and kept in the cache. A project's own assemblyscript (from npm
// install) is used when it has one; otherwise a pinned one is downloaded from
// the npm registry, checked against its sha512 integrity, and cached.
//
// The cache is ~/.jm/toolchains, or JM_TOOLCHAIN_DIR. Each entry is named by
// what's in it, so jm releases share them.
package toolchain

import (
	"archive/tar"
	"archive/zip"
	"bytes"
	"compress/gzip"
	"context"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"hash"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

// MinNodeMajor is the oldest Node a machine's own may be: AssemblyScript
// 0.28+ needs 20.
const MinNodeMajor = 20

// NodeVersion is the Node fetched when the machine has none (an LTS).
const NodeVersion = "v24.21.0"

// nodeSHA256 holds the sha256 of each platform's Node archive, from
// https://nodejs.org/dist/<NodeVersion>/SHASUMS256.txt.
var nodeSHA256 = map[string]string{
	"darwin-arm64": "bed7eea5325e1108f32ce5228ddd6a5f0f08a499ee42aa7442aea583702f6057",
	"darwin-x64":   "1462cb3b3046b815cf8ea436d3da450ec1a9f11dac7e5a46b0ada5305d7e8097",
	"linux-arm64":  "724282c3b43aec998aa9527380465b45d229e021b58035f5f4f63095eabfe5d5",
	"linux-x64":    "6e1db87ef58b8819e5d5402eff1536491b18edd8eb7bee5ef7897876e88dc5ff",
	"win-arm64":    "8779b1bde1d39f8d420e3b57aa657b39891af434d3de44a919044cec06785921",
	"win-x64":      "158f7685b44de51f6c0df1d153526cbcd3e1bc739a8dfc607721cef75de9e541",
}

// ASCVersion is the AssemblyScript fetched when a project has none.
const ASCVersion = "0.28.20"

// npmPackage is one tarball of the compiler's package tree, with the integrity
// the npm registry publishes for it. Every package-lock.json in the repo pins
// the same (TestLockfilesMatchThePinnedCompiler).
type npmPackage struct{ name, version, integrity string }

var ascPackages = []npmPackage{
	{"assemblyscript", ASCVersion, "sha512-5PM7GZpvcLypHcLmi30aP8mqm5HjU+LL3BdV1xDyA3USGtL2w+iMiUnhtH+vW8+2kOuHlAyPu7M+QgyEw5he1g=="},
	{"binaryen", "131.0.0-nightly.20260721", "sha512-AAQIkhfbYXh4FBObwBrlO+5L+6Rp6OOMOjh6AdT0M+GLNLyrwKWgtW+EzmizNgx9CWDfTJPfDtKoZi62QESbtg=="},
	{"long", "5.3.2", "sha512-mNAgZ1GmyNhD7AuqnTG3/VQ26o760+ZYBPKjPvugO8+nLbYfX6TVpJPseBvopbdY+qpZ/lKUnmEc1LeZYS3QAA=="},
}

// Where downloads come from; tests point these at a local server.
var (
	nodeDistURL = "https://nodejs.org/dist"
	npmURL      = "https://registry.npmjs.org"
)

// Toolchain is what runs a script compile or a test.
type Toolchain struct {
	Node       string `json:"node"`        // the node executable
	NodeSource string `json:"nodeSource"`  // "system" or "managed"
	NodeVer    string `json:"nodeVersion"` // as `node --version` says it
	ASC        string `json:"asc"`         // the assemblyscript package folder
	ASCSource  string `json:"ascSource"`   // "project" or "managed"
}

// Dir is the cache folder.
func Dir() (string, error) {
	if d := os.Getenv("JM_TOOLCHAIN_DIR"); d != "" {
		return filepath.Abs(d)
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(home, ".jm", "toolchains"), nil
}

// Find resolves the toolchain for the scripts package folder scriptsDir,
// downloading what's missing (progress lines go to log) unless fetch is false;
// then a missing piece is an error that says how to get it.
func Find(scriptsDir string, fetch bool, log io.Writer) (Toolchain, error) {
	var tc Toolchain
	if err := tc.findNode(fetch, log); err != nil {
		return tc, err
	}
	if err := tc.findASC(scriptsDir, fetch, log); err != nil {
		return tc, err
	}
	return tc, nil
}

func (tc *Toolchain) findNode(fetch bool, log io.Writer) error {
	// JM_TOOLCHAIN=managed ignores the machine's Node: the same compiler
	// everywhere, e.g. for goldens.
	if os.Getenv("JM_TOOLCHAIN") != "managed" {
		if path, err := exec.LookPath("node"); err == nil {
			if major, raw, err := NodeMajor(path); err == nil && major >= MinNodeMajor {
				tc.Node, tc.NodeSource, tc.NodeVer = path, "system", raw
				return nil
			}
		}
	}
	path, err := managedNode(fetch, log)
	if err != nil {
		return err
	}
	tc.Node, tc.NodeSource, tc.NodeVer = path, "managed", NodeVersion
	return nil
}

func (tc *Toolchain) findASC(scriptsDir string, fetch bool, log io.Writer) error {
	if scriptsDir != "" {
		own := filepath.Join(scriptsDir, "node_modules", "assemblyscript")
		if _, err := os.Stat(filepath.Join(own, "dist", "asc.js")); err == nil {
			tc.ASC, tc.ASCSource = own, "project"
			return nil
		}
	}
	path, err := managedASC(fetch, log)
	if err != nil {
		return err
	}
	tc.ASC, tc.ASCSource = path, "managed"
	return nil
}

// NodeMajor runs `node --version` ("v20.10.0") with a timeout, so a hung shim
// on PATH can't stall a build.
func NodeMajor(node string) (int, string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	out, err := exec.CommandContext(ctx, node, "--version").Output()
	if err != nil {
		return 0, "", err
	}
	raw := strings.TrimSpace(string(out))
	var major int
	if _, err := fmt.Sscanf(strings.TrimPrefix(raw, "v"), "%d", &major); err != nil {
		return 0, raw, fmt.Errorf("unparseable node version: %q", raw)
	}
	return major, raw, nil
}

// nodePlatform names this machine the way nodejs.org's files do.
func nodePlatform() (string, error) {
	goos := map[string]string{"darwin": "darwin", "linux": "linux", "windows": "win"}[runtime.GOOS]
	arch := map[string]string{"amd64": "x64", "arm64": "arm64"}[runtime.GOARCH]
	p := goos + "-" + arch
	if _, ok := nodeSHA256[p]; !ok {
		return "", fmt.Errorf("no Node.js download for %s/%s; install Node %d+ and put it on PATH", runtime.GOOS, runtime.GOARCH, MinNodeMajor)
	}
	return p, nil
}

func managedNode(fetch bool, log io.Writer) (string, error) {
	platform, err := nodePlatform()
	if err != nil {
		return "", err
	}
	dir, err := Dir()
	if err != nil {
		return "", err
	}
	name := "node-" + NodeVersion + "-" + platform
	exe := filepath.Join(dir, name, "node")
	if runtime.GOOS == "windows" {
		exe += ".exe"
	}
	if _, err := os.Stat(exe); err == nil {
		return exe, nil
	}
	if !fetch {
		return "", fmt.Errorf("Node.js %d+ isn't on PATH and %s isn't downloaded yet (jm doctor --fetch gets it)", MinNodeMajor, name)
	}
	ext := ".tar.gz"
	if runtime.GOOS == "windows" {
		ext = ".zip"
	}
	url := nodeDistURL + "/" + NodeVersion + "/" + name + ext
	fmt.Fprintf(log, "Downloading Node.js %s (first use; cached in %s)...\n", NodeVersion, dir)
	data, err := download(url, sha256.New(), nodeSHA256[platform], hex.EncodeToString)
	if err != nil {
		return "", err
	}
	// Only the executable: npm and the headers aren't needed to compile.
	want := name + "/bin/node"
	if runtime.GOOS == "windows" {
		want = name + "/node.exe"
	}
	err = install(filepath.Join(dir, name), func(tmp string) error {
		files := map[string]string{want: filepath.Base(exe), name + "/LICENSE": "LICENSE"}
		if ext == ".zip" {
			return unzip(data, files, tmp)
		}
		return untar(data, func(p string) string { return files[p] }, tmp)
	})
	if err != nil {
		return "", err
	}
	return exe, nil
}

func managedASC(fetch bool, log io.Writer) (string, error) {
	dir, err := Dir()
	if err != nil {
		return "", err
	}
	root := filepath.Join(dir, "assemblyscript-"+ASCVersion)
	pkg := filepath.Join(root, "node_modules", "assemblyscript")
	if _, err := os.Stat(filepath.Join(pkg, "dist", "asc.js")); err == nil {
		return pkg, nil
	}
	if !fetch {
		return "", fmt.Errorf("AssemblyScript %s isn't downloaded yet (jm doctor --fetch gets it)", ASCVersion)
	}
	fmt.Fprintf(log, "Downloading AssemblyScript %s (first use; cached in %s)...\n", ASCVersion, dir)
	tarballs := make([][]byte, len(ascPackages))
	for i, p := range ascPackages {
		algo, sum, _ := strings.Cut(p.integrity, "-")
		if algo != "sha512" {
			return "", fmt.Errorf("%s: unsupported integrity %s", p.name, algo)
		}
		base := p.name
		if i := strings.LastIndex(base, "/"); i >= 0 {
			base = base[i+1:]
		}
		url := npmURL + "/" + p.name + "/-/" + base + "-" + p.version + ".tgz"
		if tarballs[i], err = download(url, sha512.New(), sum, base64.StdEncoding.EncodeToString); err != nil {
			return "", err
		}
	}
	// A package tree node can resolve on its own: node_modules/<name>/...
	err = install(root, func(tmp string) error {
		for i, p := range ascPackages {
			prefix := filepath.Join("node_modules", filepath.FromSlash(p.name))
			err := untar(tarballs[i], func(path string) string {
				rest, ok := strings.CutPrefix(path, "package/")
				if !ok {
					return ""
				}
				return filepath.Join(prefix, filepath.FromSlash(rest))
			}, tmp)
			if err != nil {
				return fmt.Errorf("%s: %w", p.name, err)
			}
		}
		return nil
	})
	if err != nil {
		return "", err
	}
	return pkg, nil
}

// download fetches url and checks it against want (the digest as encode
// writes it), so a changed or corrupted file is never used.
func download(url string, h hash.Hash, want string, encode func([]byte) string) ([]byte, error) {
	client := &http.Client{Timeout: 10 * time.Minute}
	resp, err := client.Get(url)
	if err != nil {
		return nil, fmt.Errorf("download %s: %w (offline? install Node %d+ yourself, or set JM_TOOLCHAIN_DIR to a filled cache)", url, err, MinNodeMajor)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("download %s: %s", url, resp.Status)
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("download %s: %w", url, err)
	}
	h.Write(data)
	if got := encode(h.Sum(nil)); got != want {
		return nil, fmt.Errorf("download %s: checksum mismatch (got %s, want %s); not using it", url, got, want)
	}
	return data, nil
}

// install fills dst through a temporary sibling folder and renames it into
// place, so an interrupted download never leaves a half-filled entry.
func install(dst string, fill func(tmp string) error) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	tmp, err := os.MkdirTemp(filepath.Dir(dst), filepath.Base(dst)+".partial-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	if err := fill(tmp); err != nil {
		return err
	}
	// MkdirTemp made it 0700, which the rename keeps: a cache filled by root
	// (a container image) or shared by CI users must be readable by others.
	if err := os.Chmod(tmp, 0o755); err != nil {
		return err
	}
	if err := os.Rename(tmp, dst); err != nil {
		if _, statErr := os.Stat(dst); statErr == nil {
			return nil // another jm got there first
		}
		return err
	}
	return nil
}

// untar extracts a .tar.gz's regular files that dest maps to a relative path
// ("" skips the file) under dir.
func untar(data []byte, dest func(string) string, dir string) error {
	gz, err := gzip.NewReader(bytes.NewReader(data))
	if err != nil {
		return err
	}
	tr := tar.NewReader(gz)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		if hdr.Typeflag != tar.TypeReg {
			continue
		}
		rel := dest(hdr.Name)
		if rel == "" {
			continue
		}
		if err := writeFile(dir, rel, tr, hdr.FileInfo().Mode().Perm()); err != nil {
			return err
		}
	}
}

// unzip extracts the zip's files named in files (archive path → relative path).
func unzip(data []byte, files map[string]string, dir string) error {
	zr, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if err != nil {
		return err
	}
	for _, f := range zr.File {
		rel, ok := files[f.Name]
		if !ok {
			continue
		}
		r, err := f.Open()
		if err != nil {
			return err
		}
		err = writeFile(dir, rel, r, 0o755)
		r.Close()
		if err != nil {
			return err
		}
	}
	return nil
}

func writeFile(dir, rel string, r io.Reader, mode os.FileMode) error {
	if !filepath.IsLocal(rel) {
		return fmt.Errorf("archive path escapes its folder: %s", rel)
	}
	path := filepath.Join(dir, rel)
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode|0o200)
	if err != nil {
		return err
	}
	if _, err := io.Copy(f, r); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}
