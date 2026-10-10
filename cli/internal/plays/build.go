package plays

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// Build is a project's build as plays see it: Game fingerprints what decides
// how the game plays (scripts, scenes, prefabs, data, the manifest), Look
// everything it draws as well. Both are "" when there's no build.
type Build struct {
	Dir     string
	Game    string
	Look    string
	Changed time.Time // when its newest file was written
}

// ReadBuild fingerprints the build in dir. What runs write there (logs,
// golden runs' images) isn't the build.
func ReadBuild(dir string) Build {
	var files []string
	_ = filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() && filepath.Dir(path) == filepath.Clean(dir) && (d.Name() == "logs" || d.Name() == "golden") {
			return filepath.SkipDir
		}
		if !d.IsDir() {
			files = append(files, path)
		}
		return nil
	})
	b := Build{Dir: dir}
	if len(files) == 0 {
		return b
	}
	sort.Strings(files)
	game, look := sha256.New(), sha256.New()
	for _, f := range files {
		data, err := os.ReadFile(f)
		if err != nil {
			continue
		}
		if info, err := os.Stat(f); err == nil && info.ModTime().After(b.Changed) {
			b.Changed = info.ModTime()
		}
		rel, _ := filepath.Rel(dir, f)
		header := fmt.Sprintf("%s\x00%d\x00", filepath.ToSlash(rel), len(data))
		look.Write([]byte(header))
		look.Write(data)
		if playsDifferently(f) {
			game.Write([]byte(header))
			game.Write(data)
		}
	}
	b.Game, b.Look = short(game.Sum(nil)), short(look.Sum(nil))
	return b
}

// playsDifferently says whether a change to a build file can change how the
// game plays: scripts, data, and UI (its layout decides what a click hits).
// Images, sounds and shaders only change how it looks or sounds.
func playsDifferently(path string) bool {
	switch strings.ToLower(filepath.Ext(path)) {
	case ".ts", ".json", ".tmj", ".tsj", ".html", ".css", ".ttf", ".otf", ".fnt":
		return true
	}
	return false
}

func short(sum []byte) string { return hex.EncodeToString(sum)[:16] }

// Info is jm.json, beside the engine's files: the build a play was made with.
type Info struct {
	Build  string `json:"build"`
	Look   string `json:"look,omitempty"`
	Method int    `json:"method,omitempty"` // how Build was fingerprinted (fingerprintMethod)
	JM     string `json:"jm"`
	Pinned string `json:"pinned,omitempty"`
}

// fingerprintMethod changes when what counts as the game changes (2: UI and
// fonts count): a Build made another way can't be compared with this one.
const fingerprintMethod = 2

// Info is the play's jm.json; empty for a play jm didn't start (the editor's).
func (p *Play) Info() Info {
	var info Info
	if data, err := os.ReadFile(filepath.Join(p.Dir, "jm.json")); err == nil {
		_ = json.Unmarshal(data, &info)
	}
	return info
}

func (p *Play) writeInfo(info Info) {
	data, _ := json.Marshal(info)
	_ = os.WriteFile(filepath.Join(p.Dir, "jm.json"), data, 0o644)
}

// Drift is how a build differs from the one a play was made with.
type Drift string

const (
	Same        Drift = "same" // a replay is what the player saw
	LookChanged Drift = "look" // it plays the same, but is drawn with changed images, shaders or UI
	GameChanged Drift = "game" // it may play differently: not what the player saw
)

// DriftFrom says how b differs from the play's build. An editor play has no
// jm.json: while nothing in b is newer than the play it's the play's own, so
// the first ask pins it (a later rebuild of the same game then isn't a change).
func (p *Play) DriftFrom(b Build) Drift {
	info := p.Info()
	if info.Build == "" {
		started, err := time.Parse(time.RFC3339, p.Meta.Started)
		// Started is to the second: a build written that same second is the play's.
		if err != nil || b.Changed.IsZero() || b.Changed.After(started.Add(time.Second)) {
			return GameChanged
		}
		if b.Game == "" || p.Meta.Ended == "running" {
			return Same // the play is still being made with it
		}
		info = Info{Build: b.Game, Look: b.Look, Method: fingerprintMethod, Pinned: "after the play, by jm"}
		p.writeInfo(info)
	}
	switch {
	case info.Look != "" && info.Look == b.Look: // the look is every file: nothing changed
		return Same
	case info.Method != fingerprintMethod || info.Build != b.Game: // can't tell, or it did
		return GameChanged
	case info.Look != "":
		return LookChanged
	}
	return Same
}

// Create makes a new play's folder, named for when it starts, noting b as its
// build. .jm/ ignores itself: plays never get committed, even in a project
// whose .gitignore predates them.
func Create(projectRoot string, b Build, jmVersion string) (string, error) {
	if err := os.MkdirAll(Root(projectRoot), 0o755); err != nil {
		return "", err
	}
	if ignore := filepath.Join(projectRoot, ".jm", ".gitignore"); !exists(ignore) {
		_ = os.WriteFile(ignore, []byte("*\n"), 0o644)
	}
	base := filepath.Join(Root(projectRoot), time.Now().Format("2006-01-02_150405"))
	dir := base
	for i := 2; ; i++ {
		err := os.Mkdir(dir, 0o755)
		if err == nil {
			break
		}
		if !os.IsExist(err) {
			return "", err
		}
		dir = fmt.Sprintf("%s_%d", base, i)
	}
	(&Play{Dir: dir}).writeInfo(Info{Build: b.Game, Look: b.Look, Method: fingerprintMethod, JM: jmVersion})
	return dir, nil
}

func exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// Prune deletes all but the newest keep plays, counting only unmarked ones
// when keepMarked (a person marked something in those), and never one still
// being recorded. Folders of plays that never started go too. It says how
// many plays went.
func Prune(projectRoot string, keep int, keepMarked bool) (int, error) {
	all, err := List(projectRoot)
	if err != nil {
		return 0, err
	}
	removeUnstarted(projectRoot)
	kept, removed := 0, 0
	if !keepMarked { // by hand: the folders that aren't plays go too
		for _, dir := range Unreadable(projectRoot) {
			if err := os.RemoveAll(filepath.Join(Root(projectRoot), dir)); err == nil {
				removed++
			}
		}
	}
	for _, p := range all {
		if (keepMarked && len(p.Meta.Markers) > 0) || p.recording() {
			continue
		}
		if kept++; kept <= keep {
			continue
		}
		if err := os.RemoveAll(p.Dir); err != nil {
			return removed, err
		}
		removed++
	}
	return removed, nil
}

// recording says whether the play is still being made: the engine rewrites
// its session.json every second while it runs.
func (p *Play) recording() bool {
	info, err := os.Stat(filepath.Join(p.Dir, "session.json"))
	return p.Meta.Ended == "running" && err == nil && time.Since(info.ModTime()) < time.Minute
}

// removeUnstarted deletes play folders with no session.json an hour on: a
// game that failed to start (List can't see them, so nothing else would).
func removeUnstarted(projectRoot string) {
	entries, _ := os.ReadDir(Root(projectRoot))
	for _, e := range entries {
		dir := filepath.Join(Root(projectRoot), e.Name())
		info, err := e.Info()
		if !e.IsDir() || err != nil || exists(filepath.Join(dir, "session.json")) || time.Since(info.ModTime()) < time.Hour {
			continue
		}
		_ = os.RemoveAll(dir)
	}
}

// Unreadable are the play folders List leaves out, their session.json
// missing or damaged (Find on one says what's wrong).
func Unreadable(projectRoot string) []string {
	entries, _ := os.ReadDir(Root(projectRoot))
	var out []string
	for _, e := range entries {
		if _, err := Load(filepath.Join(Root(projectRoot), e.Name())); e.IsDir() && err != nil {
			out = append(out, e.Name())
		}
	}
	return out
}

// FramePath is where a replayed image of frame f is kept, under key (the
// build and engine that drew it).
func (p *Play) FramePath(key string, f uint64) string {
	return filepath.Join(p.Dir, "frames", key, fmt.Sprintf("%06d.png", f))
}
