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
	"time"
)

// Build is a project's build as plays see it: Game fingerprints what decides
// how the game plays (scripts, scenes, prefabs, data, the manifest), Look
// everything it draws as well. Both are "" when there's no build.
type Build struct {
	Dir  string
	Game string
	Look string
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
		rel, _ := filepath.Rel(dir, f)
		header := fmt.Sprintf("%s\x00%d\x00", filepath.ToSlash(rel), len(data))
		look.Write([]byte(header))
		look.Write(data)
		switch filepath.Ext(f) { // what the game runs; the rest only draws
		case ".ts", ".json", ".tmj", ".tsj":
			game.Write([]byte(header))
			game.Write(data)
		}
	}
	b.Game, b.Look = short(game.Sum(nil)), short(look.Sum(nil))
	return b
}

func short(sum []byte) string { return hex.EncodeToString(sum)[:16] }

// Info is jm.json, beside the engine's files: the build a play was made with.
type Info struct {
	Build  string `json:"build"`
	Look   string `json:"look,omitempty"`
	JM     string `json:"jm"`
	Pinned string `json:"pinned,omitempty"`
}

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
// jm.json: while b is older than the play it's the play's own, so the first
// ask pins it (a later rebuild of the same game then isn't a change).
func (p *Play) DriftFrom(b Build) Drift {
	info := p.Info()
	if info.Build == "" {
		built, err := os.Stat(filepath.Join(b.Dir, ".jm.json"))
		started, perr := time.Parse(time.RFC3339, p.Meta.Started)
		if err != nil || perr != nil || built.ModTime().After(started) {
			return GameChanged
		}
		if b.Game == "" || p.Meta.Ended == "running" {
			return Same // the play is still being made with it
		}
		info = Info{Build: b.Game, Look: b.Look, Pinned: "after the play, by jm"}
		p.writeInfo(info)
	}
	switch {
	case info.Build != b.Game:
		return GameChanged
	case info.Look != "" && info.Look != b.Look:
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
	(&Play{Dir: dir}).writeInfo(Info{Build: b.Game, Look: b.Look, JM: jmVersion})
	return dir, nil
}

func exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

// Prune deletes all but the newest keep plays, counting only unmarked ones
// when keepMarked (a person marked something in those). It says how many went.
func Prune(projectRoot string, keep int, keepMarked bool) (int, error) {
	all, err := List(projectRoot)
	if err != nil {
		return 0, err
	}
	kept, removed := 0, 0
	for _, p := range all {
		if keepMarked && len(p.Meta.Markers) > 0 {
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

// FramePath is where a replayed image of frame f is kept, under key (the
// build and engine that drew it).
func (p *Play) FramePath(key string, f uint64) string {
	return filepath.Join(p.Dir, "frames", key, fmt.Sprintf("%06d.png", f))
}
