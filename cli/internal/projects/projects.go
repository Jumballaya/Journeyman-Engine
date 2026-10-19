// Package projects is the person's games: the ones they opened (the editor's
// recent projects, which jm keeps too) and the ones in the games folder.
package projects

import (
	"encoding/json"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/filelock"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// Project is a game the person has.
type Project struct {
	Name    string   `json:"name"`
	Path    string   `json:"path"`
	Opened  int64    `json:"opened"` // unix seconds; 0: never opened
	Scenes  []string `json:"scenes"`
	Missing bool     `json:"missing,omitempty"` // moved or deleted since
	// A variant's or sandbox's folder opened as a game: the game it's a copy of,
	// which kind ("variant", "sandbox") and its name. Empty for a game's own.
	CopyOf   string `json:"copyOf,omitempty"`
	CopyKind string `json:"copyKind,omitempty"`
	Copy     string `json:"copy,omitempty"`
	// Thumb is an image of the game (its newest play or golden frame); "" if none.
	Thumb string `json:"-"`
}

// entry is one project in the editor's recent.json (Project.cpp's updateRecents).
type entry struct {
	Path   string `json:"path"`
	Name   string `json:"name"`
	Opened int64  `json:"opened"`
}

// List is every project, newest opened first, then the games folder's others.
func List(gamesFolder string) []Project {
	var out []Project
	seen := map[string]bool{}
	for _, e := range load() {
		seen[e.Path] = true
		out = append(out, describe(e))
	}
	dirs, _ := os.ReadDir(gamesFolder)
	for _, d := range dirs {
		dir := canonical(filepath.Join(gamesFolder, d.Name()))
		if d.IsDir() && isGame(dir) && !seen[dir] {
			out = append(out, describe(entry{Path: dir}))
		}
	}
	return out
}

// Remember puts the game in dir first, opened now.
func Remember(dir string) error {
	dir = canonical(dir)
	return update(func(list []entry) []entry {
		list = slices.DeleteFunc(list, func(e entry) bool { return e.Path == dir })
		return append([]entry{{dir, nameOf(dir, ""), time.Now().Unix()}}, list...)
	})
}

// Add puts the game in dir on the list, last and never opened, unless it's on it.
func Add(dir string) error {
	dir = canonical(dir)
	return update(func(list []entry) []entry {
		if slices.ContainsFunc(list, func(e entry) bool { return e.Path == dir }) {
			return list
		}
		return append(list, entry{dir, nameOf(dir, ""), 0})
	})
}

// Forget takes dir off the list (its files stay).
func Forget(dir string) error {
	dir = canonical(dir)
	return update(func(list []entry) []entry {
		return slices.DeleteFunc(list, func(e entry) bool { return e.Path == dir })
	})
}

// Scan adds every game in root and the folders below it, and gives back the new ones.
func Scan(root string) ([]string, error) { return scan(root, 6) }

// FindGames adds the games in the person's home folder (Documents, Desktop,
// Developer…), up to 4 folders down, and gives back the new ones.
func FindGames(home string) ([]string, error) { return scan(home, 4) }

func scan(root string, maxDepth int) ([]string, error) {
	var found []string
	root = canonical(root)
	err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
		if err != nil && path == root {
			return err
		}
		if err != nil || !d.IsDir() {
			return nil // an unreadable folder below the root holds no games we can see
		}
		name := d.Name()
		if path != root && (strings.HasPrefix(name, ".") || slices.Contains([]string{"build", "dist", "node_modules", "logs", "Library"}, name)) {
			return filepath.SkipDir
		}
		if isGame(path) {
			found = append(found, path)
			return filepath.SkipDir // a game's own folders hold no games
		}
		if depth := strings.Count(strings.TrimPrefix(path, root), string(filepath.Separator)); depth >= maxDepth {
			return filepath.SkipDir
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	var added []string
	err = update(func(known []entry) []entry {
		added = nil
		for _, dir := range found {
			if !slices.ContainsFunc(known, func(e entry) bool { return e.Path == dir }) {
				added = append(added, dir)
				known = append(known, entry{dir, nameOf(dir, ""), 0})
			}
		}
		return known
	})
	return added, err
}

func describe(e entry) Project {
	p := Project{Name: nameOf(e.Path, e.Name), Path: e.Path, Opened: e.Opened, Scenes: []string{}}
	p.CopyOf, p.CopyKind, p.Copy = CopyOf(e.Path) // a dropped copy too: it's its game's, not a missing game
	man, err := manifest.LoadManifest(filepath.Join(e.Path, archive.ManifestEntryKey))
	if err != nil {
		p.Missing = !isGame(e.Path)
		return p
	}
	if man.Scenes != nil {
		p.Scenes = man.Scenes
	}
	p.Thumb = Thumb(e.Path)
	return p
}

// WithCopies is list as a game picker shows it: a copy (a variant's or
// sandbox's folder) is listed under its game, so the open folder when it's a
// copy not in list is added, and each copy's game list doesn't have.
func WithCopies(list []Project, open string) []Project {
	listed := func(dir string) bool { return slices.ContainsFunc(list, func(p Project) bool { return p.Path == dir }) }
	if open != "" {
		if open = canonical(open); !listed(open) {
			if g, _, _ := CopyOf(open); g != "" {
				list = append(list, describe(entry{Path: open}))
			}
		}
	}
	for _, p := range list {
		if p.CopyOf != "" && !listed(p.CopyOf) {
			list = append(list, describe(entry{Path: p.CopyOf}))
		}
	}
	return list
}

// inRepo is where dir sits in its git repository ("games/pip"), "" at its top or outside one.
func inRepo(dir string) string {
	c := exec.Command("git", "rev-parse", "--show-prefix")
	c.Dir = dir
	out, err := c.Output()
	if err != nil {
		return ""
	}
	return filepath.FromSlash(strings.TrimSuffix(strings.TrimSuffix(string(out), "\n"), "/"))
}

// CopyOf finds the game a folder is a copy of: <game>/.jm/sandboxes/<name>
// (the game's folder copied), or <game>/.jm/variants/<name>[/<the game's
// folder in its repository>] (a worktree). The game must be one.
func CopyOf(dir string) (game, kind, name string) {
	parts := strings.Split(filepath.ToSlash(filepath.Clean(dir)), "/")
	for i := len(parts) - 3; i >= 1; i-- {
		if parts[i] != ".jm" || (parts[i+1] != "variants" && parts[i+1] != "sandboxes") || (parts[i+1] == "sandboxes" && i+3 != len(parts)) {
			continue
		}
		g := filepath.FromSlash(strings.Join(parts[:i], "/"))
		if g == "" {
			g = string(filepath.Separator)
		}
		// A variant's game sits where the game sits in its repository (vcs.GameIn), not anywhere below.
		if isGame(g) && (parts[i+1] == "sandboxes" || filepath.Clean(dir) == filepath.Join(g, ".jm", "variants", parts[i+2], inRepo(g))) {
			return g, map[string]string{"variants": "variant", "sandboxes": "sandbox"}[parts[i+1]], parts[i+2]
		}
	}
	return "", "", ""
}

// Thumb is a picture of the game in dir: the newest play's last thumbnail, else the newest golden frame.
func Thumb(dir string) string {
	list, _ := plays.List(dir)
	for _, p := range list {
		if thumbs := p.Thumbs(); len(thumbs) > 0 {
			return thumbs[len(thumbs)-1].Path
		}
	}
	frames, _ := filepath.Glob(filepath.Join(dir, "tests", "golden", "*", "frame_*.png"))
	newest, newestTime := "", time.Time{}
	for _, f := range frames {
		if info, err := os.Stat(f); err == nil && info.ModTime().After(newestTime) {
			newest, newestTime = f, info.ModTime()
		}
	}
	return newest
}

func nameOf(dir, fallback string) string {
	if man, err := manifest.LoadManifest(filepath.Join(dir, archive.ManifestEntryKey)); err == nil && man.Name != "" {
		return man.Name
	}
	if fallback != "" {
		return fallback
	}
	return filepath.Base(dir)
}

func isGame(dir string) bool {
	info, err := os.Stat(filepath.Join(dir, archive.ManifestEntryKey))
	return err == nil && !info.IsDir()
}

// canonical is dir as the editor keys it (std::filesystem::canonical).
func canonical(dir string) string {
	abs, err := filepath.Abs(dir)
	if err != nil {
		return dir
	}
	if real, err := filepath.EvalSymlinks(abs); err == nil {
		return real
	}
	return abs
}

func load() []entry {
	var list []entry
	data, _ := os.ReadFile(File())
	_ = json.Unmarshal(data, &list)
	return slices.DeleteFunc(list, func(e entry) bool { return e.Path == "" })
}

// update replaces the list with change's, locked against every other jm and
// editor (Project.cpp's updateRecents) so none of their changes are lost.
func update(change func([]entry) []entry) error {
	release, err := filelock.Hold(File())
	if err != nil {
		return err
	}
	defer release()
	data, err := json.MarshalIndent(change(load()), "", "  ")
	if err != nil {
		return err
	}
	return atomicfile.WriteFile(File(), data, 0o644)
}

// File is the list: recent.json in the editor's settings folder.
func File() string { return settingsFile("recent.json") }

// PreferencesFile is the editor's preferences (Project.cpp's preference()).
func PreferencesFile() string { return settingsFile("preferences.json") }

// settingsFile is name in the editor's settings folder
// (engine/core/app/Platform.cpp's userDataDir("Journeyman Editor")).
func settingsFile(name string) string {
	const app = "Journeyman Editor"
	home := os.Getenv("HOME")
	dir := filepath.Join(os.TempDir(), app)
	switch runtime.GOOS {
	case "darwin":
		if home != "" {
			dir = filepath.Join(home, "Library", "Application Support", app)
		}
	case "windows":
		if appData := os.Getenv("APPDATA"); appData != "" {
			dir = filepath.Join(appData, app)
		}
	default:
		if xdg := os.Getenv("XDG_DATA_HOME"); xdg != "" {
			dir = filepath.Join(xdg, app)
		} else if home != "" {
			dir = filepath.Join(home, ".local", "share", app)
		}
	}
	return filepath.Join(dir, name)
}
