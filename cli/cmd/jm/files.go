package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"unicode/utf8"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
	"github.com/Jumballaya/Journeyman-Engine/internal/gametree"
	"github.com/Jumballaya/Journeyman-Engine/internal/jsonfmt"
	"github.com/Jumballaya/Journeyman-Engine/internal/realpath"
	"github.com/Jumballaya/Journeyman-Engine/internal/schema"
	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"
	"github.com/spf13/cobra"
)

// A game's files as a person manages them: the tree with git's view of each
// file, and reads and writes that keep jm's JSON layout and never leave the
// game's folder (nor go into the folders hidden from agents: .git, build…).

var filesCmd = &cobra.Command{
	Use:   "files",
	Short: "The game's files, with what git says changed",
	Long: `Lists the game's files (not .git, .jm, build, dist or node_modules) with
their sizes and git's mark for each changed one: M changed, A added, ? new
to git, D deleted, U in conflict.

  jm files                  the list (--json for tools)
  jm files search <text>    files whose name or lines hold text (any case)`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		root, err := projectRoot()
		if err != nil {
			return err
		}
		list, err := listFiles(root)
		if err != nil {
			return err
		}
		if jsonOutput {
			return writeJSON(cmd.OutOrStdout(), map[string]any{"files": list})
		}
		for _, f := range list {
			fmt.Fprintf(cmd.OutOrStdout(), "%-2s %s  %s\n", f.Status, f.Path, sizeText(f.Size))
		}
		return nil
	},
}

func init() {
	filesCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	filesCmd.AddCommand(&cobra.Command{Use: "search <text>", Short: "Files whose name or lines hold text (any case)", Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			root, err := projectRoot()
			if err != nil {
				return err
			}
			found := searchFiles(root, args[0], searchLimit)
			if jsonOutput {
				return writeJSON(cmd.OutOrStdout(), found)
			}
			for _, h := range found.Hits {
				if h.Line == 0 {
					fmt.Fprintf(cmd.OutOrStdout(), "%s\n", h.Path)
				} else {
					fmt.Fprintf(cmd.OutOrStdout(), "%s:%d: %s\n", h.Path, h.Line, h.Text)
				}
			}
			if found.Truncated {
				fmt.Fprintf(cmd.OutOrStdout(), "(the first %d: search for something narrower)\n", searchLimit)
			}
			return nil
		}})
}

// projectEntry is one file of the game. Path is from the game's folder, with
// slashes; Status is git's mark ("" unchanged, or not in a repository).
type projectEntry struct {
	Path   string `json:"path"`
	Size   int64  `json:"size"`
	Status string `json:"status,omitempty"`
}

// listFiles is every file of the game sorted by path, and the files git
// says were deleted (status D, size 0).
func listFiles(root string) ([]projectEntry, error) {
	status := vcs.Status(root)
	out := []projectEntry{}
	_ = walkProject(root, func(rel string, info fs.FileInfo) { // a folder it can't read is left out of a listing
		out = append(out, projectEntry{Path: rel, Size: info.Size(), Status: status[rel]})
		delete(status, rel)
	})
	for path, mark := range status {
		if mark == "D" && !hiddenFromAgents(filepath.Dir(path)) && !osClutter(path) {
			out = append(out, projectEntry{Path: path, Status: mark})
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Path < out[j].Path })
	return out, nil
}

// osClutter is a file the OS leaves in folders: its, not the game's.
func osClutter(path string) bool {
	switch strings.ToLower(filepath.Base(path)) {
	case ".ds_store", "thumbs.db", "desktop.ini":
		return true
	}
	return false
}

// walkProject calls visit with each regular file of the game outside the hidden folders.
func walkProject(root string, visit func(rel string, info fs.FileInfo)) error {
	var unreadable error
	err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			if unreadable == nil {
				unreadable = err
			}
			return nil // a folder it can't read is left out, not the end of the list (but said, after)
		}
		rel, _ := filepath.Rel(root, path)
		if d.IsDir() {
			if rel != "." && hiddenFromAgents(rel) {
				return filepath.SkipDir
			}
			return nil
		}
		if d.Name() == ".git" { // a worktree's link to its repository: git's, not the game's
			return nil
		}
		if osClutter(d.Name()) {
			return nil
		}
		if info, err := d.Info(); err == nil && info.Mode().IsRegular() {
			visit(filepath.ToSlash(rel), info)
		}
		return nil
	})
	if err != nil {
		return err
	}
	return unreadable
}

// fileEtag names a version of a file's contents; a missing file's is "".
func fileEtag(data []byte) string {
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:8])
}

// gamePath is path (from the game's folder, or absolute) as the real file it
// names (links resolved) and its path from the game: an error for one outside
// the game or in a hidden folder. The file needn't exist yet.
func gamePath(root, path string) (string, string, error) {
	if root == "" {
		return "", "", errNoGame
	}
	if path == "" {
		return "", "", fmt.Errorf("give a file's path, e.g. scenes/main.scene.json")
	}
	real, rel, ok := realpath.Rel(root, filepath.FromSlash(path))
	if !ok || hiddenFromAgents(filepath.Dir(rel)) || hiddenFromAgents(rel) && isDir(real) {
		return "", "", fmt.Errorf("%s isn't one of the game's files", path)
	}
	return real, rel, nil
}

// contentFile is gamePath for a path the game's own files name (an atlas's
// source, a sprite's image): read as the build reads it, its ".." cleaned away.
func contentFile(root, ref string) (string, error) {
	real, _, err := gamePath(root, path.Clean(filepath.ToSlash(ref)))
	return real, err
}

func isDir(path string) bool {
	info, err := os.Stat(path)
	return err == nil && info.IsDir()
}

// fileText is a file as an editor shows it: its text (empty for one that
// isn't UTF-8 text, Binary then), size, etag and what's wrong with it.
type fileText struct {
	Path   string `json:"path"`
	Text   string `json:"text"`
	Binary bool   `json:"binary,omitempty"`
	Size   int    `json:"size"`
	Etag   string `json:"etag"`
	// Problems: what fileProblems finds.
	Problems []string `json:"problems,omitempty"`
}

// textLimit: a file bigger than this is shown as binary (a level that size is data, not text to edit).
const textLimit = 2 << 20

func readGameFile(root, path string) (fileText, error) {
	abs, rel, err := gamePath(root, path)
	if err != nil {
		return fileText{}, err
	}
	data, err := os.ReadFile(abs)
	if err != nil {
		return fileText{}, fmt.Errorf("%s: no such file in the game", rel)
	}
	f := fileText{Path: rel, Size: len(data), Etag: fileEtag(data)}
	if len(data) > textLimit || !utf8.Valid(data) || bytes.IndexByte(data, 0) >= 0 {
		f.Binary = true
	} else {
		f.Text = string(data)
		f.Problems = fileProblems(rel, data)
	}
	return f, nil
}

// errChanged: a write's etag isn't the file's now (someone else saved it since).
var errChanged = errors.New("the file changed since it was read")

// saved is what writeGameFile did: the file's new etag and text as saved
// (jm's JSON in jm fmt's layout), and what's wrong with it (fileProblems:
// saved anyway, as jm build only warns).
type saved struct {
	Path     string   `json:"path"`
	Etag     string   `json:"etag"`
	Text     string   `json:"text"`
	Created  bool     `json:"created,omitempty"`
	Problems []string `json:"problems,omitempty"`
}

// writeGameFile saves text as path, making its folders. With an etag, only
// over that version of the file ("": only when there's no file yet), else
// errChanged. JSON that doesn't parse isn't saved.
func writeGameFile(root, path, text string, etag *string) (saved, error) {
	release, err := gamelock.Hold()
	if err != nil {
		return saved{}, err
	}
	defer release()
	abs, rel, err := gamePath(root, path)
	if err != nil {
		return saved{}, err
	}
	old, readErr := os.ReadFile(abs)
	now := ""
	if readErr == nil {
		now = fileEtag(old)
	} else if isDir(abs) {
		return saved{}, fmt.Errorf("%s is a folder", rel)
	} else if !errors.Is(readErr, fs.ErrNotExist) {
		return saved{}, readErr
	}
	if etag != nil && *etag != now {
		return saved{}, errChanged
	}
	data := []byte(text)
	if strings.HasSuffix(rel, ".json") {
		formatted, err := jsonfmt.Format(data)
		if err != nil {
			return saved{}, fmt.Errorf("%s isn't valid JSON, so it wasn't saved: %v", rel, err)
		}
		if formattable(rel) {
			data = formatted
		}
	}
	if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
		return saved{}, err
	}
	if err := atomicfile.WriteFile(abs, data, 0o644); err != nil {
		return saved{}, err
	}
	return saved{Path: rel, Etag: fileEtag(data), Text: string(data), Created: readErr != nil, Problems: fileProblems(rel, data)}, nil
}

// fileProblems is what's wrong with a file as jm sees it: JSON that doesn't
// parse or isn't in jm fmt's layout, and a scene's or prefab's problems with
// the engine's schema (unchecked when there's no engine to ask).
func fileProblems(rel string, data []byte) []string {
	if !strings.HasSuffix(rel, ".json") {
		return nil
	}
	formatted, err := jsonfmt.Format(data)
	if err != nil {
		return []string{fmt.Sprintf("not valid JSON: %v", err)}
	}
	var out []string
	if formattable(rel) && !bytes.Equal(formatted, data) {
		out = append(out, "not in jm fmt's layout (jm fmt rewrites it)")
	}
	if !strings.HasSuffix(rel, ".scene.json") && !strings.HasSuffix(rel, ".prefab.json") {
		return out
	}
	engine, err := resolveEnginePath()
	if err != nil {
		return out
	}
	s, err := schema.FromEngine(engine)
	if err != nil {
		return out
	}
	for _, p := range s.Check(rel, data) {
		line := p.String()
		if p.Line > 0 {
			line = fmt.Sprintf("%s (line %d)", line, p.Line)
		}
		out = append(out, line)
	}
	return out
}

// moveGameFile renames a file or folder of the game, making the folders to
// it; to mustn't exist yet.
func moveGameFile(root, from, to string) (string, string, error) {
	release, err := gamelock.Hold()
	if err != nil {
		return "", "", err
	}
	defer release()
	src, fromRel, err := gamePath(root, from)
	if err != nil {
		return "", "", err
	}
	dst, toRel, err := gamePath(root, to)
	if err != nil {
		return "", "", err
	}
	switch {
	case !exists(src):
		return "", "", fmt.Errorf("%s: no such file in the game", fromRel)
	case strings.EqualFold(fromRel, archive.ManifestEntryKey):
		return "", "", fmt.Errorf("%s is what makes the folder a game: it stays", fromRel)
	case exists(dst):
		return "", "", fmt.Errorf("%s already exists", toRel)
	case strings.HasPrefix(toRel+"/", fromRel+"/"):
		return "", "", fmt.Errorf("%s can't go inside itself", fromRel)
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return "", "", err
	}
	return fromRel, toRel, os.Rename(src, dst)
}

// deleteGameFile removes a file or a folder and all in it; deleting one
// that isn't there does nothing.
func deleteGameFile(root, path string) (string, error) {
	release, err := gamelock.Hold()
	if err != nil {
		return "", err
	}
	defer release()
	abs, rel, err := gamePath(root, path)
	if err != nil {
		return "", err
	}
	if rel == "." || strings.EqualFold(rel, archive.ManifestEntryKey) {
		return "", fmt.Errorf("%s is what makes the folder a game: it stays", archive.ManifestEntryKey)
	}
	if _, err := os.Lstat(abs); errors.Is(err, fs.ErrNotExist) {
		return rel, nil
	}
	// Moved aside in one step, then removed: deleting takes many, and a lock
	// file in there (a game holding the settings folder) mustn't go partway
	// through, as the next jm would start while this one still deletes.
	aside, err := os.MkdirTemp(filepath.Dir(abs), ".jm-deleting-") // hidden from the file tools
	if err != nil {
		return "", err
	}
	if err := os.Rename(abs, filepath.Join(aside, filepath.Base(abs))); err != nil {
		os.Remove(aside)
		return "", err
	}
	return rel, os.RemoveAll(aside)
}

// searchHit is a file whose name holds the text (Line 0), or one of its lines that does.
type searchHit struct {
	Path string `json:"path"`
	Line int    `json:"line,omitempty"`
	Text string `json:"text,omitempty"`
}

type searchResult struct {
	Hits      []searchHit `json:"hits"`
	Truncated bool        `json:"truncated,omitempty"`
}

const searchLimit = 200

// searchFiles finds text, in any case, in the game's file names and text
// files' lines: the first limit hits, in path order.
func searchFiles(root, text string, limit int) searchResult {
	result := searchResult{Hits: []searchHit{}}
	needle := strings.ToLower(text)
	if needle == "" {
		return result
	}
	add := func(h searchHit) bool {
		if len(result.Hits) == limit {
			result.Truncated = true
			return false
		}
		result.Hits = append(result.Hits, h)
		return true
	}
	files, _ := listFiles(root)
	for _, f := range files {
		if strings.Contains(strings.ToLower(f.Path), needle) && !add(searchHit{Path: f.Path}) {
			return result
		}
		if f.Size == 0 || f.Size > textLimit {
			continue
		}
		data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(f.Path)))
		if err != nil || !utf8.Valid(data) {
			continue
		}
		for i, line := range strings.Split(string(data), "\n") {
			if strings.Contains(strings.ToLower(line), needle) && !add(searchHit{Path: f.Path, Line: i + 1, Text: clip(strings.TrimSpace(line), 160)}) {
				return result
			}
		}
	}
	return result
}

func clip(s string, n int) string {
	if r := []rune(s); len(r) > n {
		return string(r[:n]) + "…"
	}
	return s
}

func sizeText(n int64) string {
	switch {
	case n >= 1<<20:
		return fmt.Sprintf("%.1f MB", float64(n)/(1<<20))
	case n >= 1<<10:
		return fmt.Sprintf("%.1f KB", float64(n)/(1<<10))
	}
	return fmt.Sprintf("%d B", n)
}

// codeEditors are the editors a text file opens in, the first found (as the
// editor's Edit Script does): macOS bundle id, and their tool's name elsewhere.
var codeEditors = []struct{ name, bundle, tool string }{
	{"Visual Studio Code", "com.microsoft.VSCode", "code"},
	{"Cursor", "com.todesktop.230313mzl4w4u92", "cursor"},
	{"Zed", "dev.zed.Zed", "zed"},
	{"Sublime Text", "com.sublimetext.4", "subl"},
}

// openOnDesktop opens a file of the game on the person's screen: a text file
// in their code editor (else the plain text editor), others in their default
// app. It says which.
func openOnDesktop(abs string, text bool) (string, error) {
	if text {
		for _, e := range codeEditors {
			if runtime.GOOS == "darwin" {
				if exec.Command("open", "-b", e.bundle, abs).Run() == nil {
					return e.name, nil
				}
			} else if tool, err := exec.LookPath(e.tool); err == nil && (runtime.GOOS != "windows" || strings.HasSuffix(strings.ToLower(tool), ".exe")) {
				return e.name, startAndForget(exec.Command(tool, abs))
			}
		}
	}
	switch runtime.GOOS {
	case "darwin":
		if text { // `open` gives a .ts to a video player
			return "TextEdit", exec.Command("open", "-t", abs).Run()
		}
		return "its default app", exec.Command("open", abs).Run()
	case "windows":
		// Not cmd's start: cmd reads & in a file name as a command.
		return "its default app", exec.Command("rundll32", "url.dll,FileProtocolHandler", abs).Run()
	}
	return "its default app", startAndForget(exec.Command("xdg-open", abs))
}

func startAndForget(cmd *exec.Cmd) error {
	if err := cmd.Start(); err != nil {
		return err
	}
	go func() { _ = cmd.Wait() }()
	return nil
}

// realPath is path absolute, with every symlink on the way resolved.
func realPath(path string) (string, error) {
	abs, err := filepath.Abs(path)
	if err != nil {
		return "", err
	}
	return filepath.EvalSymlinks(abs)
}

// hiddenFromAgents says whether a folder of the project (a path from its
// folder) is one resources leave out: in a folder that isn't the game's (gametree).
func hiddenFromAgents(dir string) bool { return gametree.Skip(filepath.ToSlash(dir)) }

// eachGameFile calls visit with each file of the game agents see: its slash
// path from root and its real file (a link counts while it stays in the game).
func eachGameFile(root string, visit func(rel, real string)) {
	if root == "" { // no game open
		return
	}
	_ = filepath.WalkDir(root, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		rel, _ := filepath.Rel(root, path)
		if d.IsDir() {
			if rel != "." && hiddenFromAgents(rel) {
				return filepath.SkipDir
			}
			return nil
		}
		if real, _, err := gamePath(root, rel); err == nil && !isDir(real) { // a link keeps its own name
			visit(filepath.ToSlash(rel), real)
		}
		return nil
	})
}

// gameFiles are the paths of the game's files agents see that keep says to, sorted.
func gameFiles(root string, keep func(rel string) bool) []string {
	var files []string
	eachGameFile(root, func(rel, _ string) {
		if keep(rel) {
			files = append(files, rel)
		}
	})
	sort.Strings(files)
	return files
}
