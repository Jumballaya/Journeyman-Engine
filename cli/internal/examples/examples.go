// Package examples gets the example games of a jm release: the repo's demos/,
// published as journeyman-examples.zip beside the release's other files. The
// first use downloads it, checked against the release's SHA256SUMS, into
// ~/.jm/examples/<version>; Copy makes a new game from one.
package examples

import (
	"archive/zip"
	"bufio"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/download"
	"github.com/Jumballaya/Journeyman-Engine/internal/jsonfmt"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// An Example is one example game, in the cache.
type Example struct {
	Name  string `json:"name"`  // its folder: what Copy takes
	Title string `json:"title"` // the game's name, from its .jm.json
	Path  string `json:"path"`
}

// asset is the release file the examples come in.
const asset = "journeyman-examples.zip"

// releasesURL holds each release's files at <releasesURL>/<version>/<file>.
var releasesURL = "https://github.com/Jumballaya/Journeyman-Engine/releases/download"

// List gives the examples of release version (as `jm --version` names it),
// downloading them first if they aren't cached; progress lines go to log.
func List(ctx context.Context, version string, log io.Writer) ([]Example, error) {
	dir, err := fetch(ctx, version, log)
	if err != nil {
		return nil, err
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	var out []Example
	for _, e := range entries {
		path := filepath.Join(dir, e.Name())
		m, err := manifest.LoadManifest(filepath.Join(path, archive.ManifestEntryKey))
		if err != nil {
			continue // not a game: common/, the README
		}
		out = append(out, Example{Name: e.Name(), Title: m.Name, Path: path})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out, nil
}

func fetch(ctx context.Context, version string, log io.Writer) (string, error) {
	if !strings.HasPrefix(version, "v") {
		return "", fmt.Errorf("this jm (%s) isn't a release, so it has no examples to download: a checkout has them in demos/", version)
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", err
	}
	dir := filepath.Join(home, ".jm", "examples", version)
	if _, err := os.Stat(dir); err == nil {
		return dir, nil
	}
	sums, err := download.Get(ctx, releasesURL+"/"+version+"/SHA256SUMS")
	if err != nil {
		return "", err
	}
	want := sumOf(sums, asset)
	if want == "" {
		return "", fmt.Errorf("release %s has no %s", version, asset)
	}
	fmt.Fprintf(log, "Downloading the %s examples (first use; kept in %s)...\n", version, dir)
	data, err := download.Checked(ctx, releasesURL+"/"+version+"/"+asset, sha256.New(), want, hex.EncodeToString)
	if err != nil {
		return "", err
	}
	return dir, download.Install(dir, func(tmp string) error { return unzip(data, tmp) })
}

// sumOf finds name's digest in a SHA256SUMS file.
func sumOf(sums []byte, name string) string {
	lines := bufio.NewScanner(bytes.NewReader(sums))
	for lines.Scan() {
		sum, file, ok := strings.Cut(lines.Text(), "  ")
		if ok && strings.TrimPrefix(file, "*") == name {
			return sum
		}
	}
	return ""
}

// unzip extracts the archive's files from under its top folder into dir.
func unzip(data []byte, dir string) error {
	zr, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if err != nil {
		return err
	}
	for _, f := range zr.File {
		_, rel, ok := strings.Cut(f.Name, "/")
		if !ok || rel == "" || f.FileInfo().IsDir() {
			continue
		}
		r, err := f.Open()
		if err != nil {
			return err
		}
		err = download.WriteFile(dir, filepath.FromSlash(rel), r, f.Mode().Perm())
		r.Close()
		if err != nil {
			return err
		}
	}
	return nil
}

// skipped are a project's folders that aren't its sources.
var skipped = map[string]bool{"build": true, "dist": true, ".jm": true, "logs": true, "node_modules": true, ".git": true}

// Copy makes a new game, named for the folder to (empty or new), from the
// example called name: its sources, and the script libraries it shares with
// other examples (../common), which come along into libraries/.
func Copy(ctx context.Context, version, name, to string, log io.Writer) error {
	if entries, err := os.ReadDir(to); err == nil && len(entries) > 0 {
		return fmt.Errorf("%s already exists and isn't empty", to)
	}
	list, err := List(ctx, version, log)
	if err != nil {
		return err
	}
	i := slices.IndexFunc(list, func(ex Example) bool { return ex.Name == name })
	if i < 0 {
		return fmt.Errorf("no example %s (jm examples lists them)", name)
	}
	ex := list[i]
	to, err = filepath.Abs(to)
	if err != nil {
		return err
	}
	manifestPath := filepath.Join(ex.Path, archive.ManifestEntryKey)
	original, err := os.ReadFile(manifestPath)
	if err != nil {
		return err
	}
	var m map[string]any
	if err := json.Unmarshal(original, &m); err != nil {
		return fmt.Errorf("%s: %w", manifestPath, err)
	}
	m["name"] = filepath.Base(to)
	libraries, _ := m["scriptLibraries"].(map[string]any)
	type copying struct{ from, to string }
	copies := []copying{{ex.Path, to}}
	for name, v := range libraries {
		rel, _ := v.(string)
		if rel == "" || filepath.IsLocal(filepath.FromSlash(rel)) {
			continue // inside the game: copied with it
		}
		local := "libraries/" + strings.ReplaceAll(strings.TrimPrefix(name, "@"), "/", "-")
		copies = append(copies, copying{filepath.Join(ex.Path, filepath.FromSlash(rel)), filepath.Join(to, filepath.FromSlash(local))})
		libraries[name] = local
	}
	for _, c := range copies {
		if inside(c.from, to) {
			return fmt.Errorf("a copy can't go inside %s, which it copies", c.from)
		}
	}
	for _, c := range copies {
		if err := copySources(c.from, c.to); err != nil {
			return err
		}
	}
	edited, err := json.Marshal(m)
	if err == nil {
		edited, err = jsonfmt.FormatKeeping(edited, original)
	}
	if err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(to, archive.ManifestEntryKey), edited, 0o644)
}

// inside says whether path is dir or under it, by the filesystem's identity
// of folders: symlinks and case-insensitive names can't hide it.
func inside(dir, path string) bool {
	want, err := os.Stat(dir)
	if err != nil {
		return false
	}
	for p := realPath(path); ; p = filepath.Dir(p) {
		if got, err := os.Stat(p); err == nil && os.SameFile(want, got) {
			return true
		}
		if filepath.Dir(p) == p {
			return false
		}
	}
}

// realPath is path with the symlinks in its nearest existing ancestor
// resolved, so a link into a folder counts as that folder.
func realPath(path string) string {
	rest := ""
	for dir := path; ; dir = filepath.Dir(dir) {
		if real, err := filepath.EvalSymlinks(dir); err == nil {
			return filepath.Join(real, rest)
		}
		if filepath.Dir(dir) == dir {
			return path
		}
		rest = filepath.Join(filepath.Base(dir), rest)
	}
}

func copySources(from, to string) error {
	return filepath.WalkDir(from, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		rel, _ := filepath.Rel(from, path)
		if d.IsDir() {
			if skipped[d.Name()] && path != from {
				return filepath.SkipDir
			}
			return os.MkdirAll(filepath.Join(to, rel), 0o755)
		}
		if !d.Type().IsRegular() {
			return nil
		}
		f, err := os.Open(path)
		if err != nil {
			return err
		}
		defer f.Close()
		info, err := f.Stat()
		if err != nil {
			return err
		}
		return download.WriteFile(to, rel, f, info.Mode().Perm())
	})
}
