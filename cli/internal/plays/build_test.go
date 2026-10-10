package plays

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// writeSession makes a play folder with session.json fields.
func writeSession(t *testing.T, root, id, fields string) string {
	t.Helper()
	dir := filepath.Join(Root(root), id)
	os.MkdirAll(dir, 0o755)
	os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":1,`+fields+`}`), 0o644)
	return dir
}

func TestOldUnmarkedPlaysArePrunedMarkedOnesKept(t *testing.T) {
	root := t.TempDir()
	writeSession(t, root, "2000-01-01_000000", `"markers":[{"n":1,"frame":0}]`) // the oldest, but marked
	for i := 1; i <= 45; i++ {
		writeSession(t, root, fmt.Sprintf("2000-01-02_%06d", i), `"markers":[]`)
	}
	if removed, err := Prune(root, 40, true); err != nil || removed != 5 {
		t.Fatalf("removed %d, %v; want 5", removed, err)
	}
	all, _ := List(root)
	if len(all) != 41 || all[len(all)-1].ID != "2000-01-01_000000" || all[0].ID != "2000-01-02_000045" {
		t.Fatalf("%d plays left, oldest %s", len(all), all[len(all)-1].ID)
	}
	if removed, _ := Prune(root, 1, false); removed != 40 {
		t.Errorf("pruning by hand removed %d; want all but one, marked or not", removed)
	}
}

// An editor play has no jm.json: its build is the one it was made with while
// that's older than the play, so the first look pins it.
func TestAnEditorPlaysBuildIsPinnedWhileUnchanged(t *testing.T) {
	root := t.TempDir()
	build := filepath.Join(root, "build")
	os.MkdirAll(build, 0o755)
	os.WriteFile(filepath.Join(build, ".jm.json"), []byte(`{}`), 0o644)
	os.WriteFile(filepath.Join(build, "game.json"), []byte(`{"speed":1}`), 0o644)
	old := time.Now().Add(-time.Hour)
	os.Chtimes(filepath.Join(build, ".jm.json"), old, old)
	started := time.Now().Add(-time.Minute).Format(time.RFC3339)
	p, err := Load(writeSession(t, root, "2000-01-01_000000", `"ended":"quit","started":"`+started+`"`))
	if err != nil {
		t.Fatal(err)
	}
	if p.DriftFrom(ReadBuild(build)) != Same {
		t.Fatal("the build the play ran counts as a change")
	}
	if p.Info().Build != ReadBuild(build).Game {
		t.Fatal("the build wasn't pinned to the play")
	}
	os.Chtimes(filepath.Join(build, ".jm.json"), time.Now(), time.Now()) // rebuilt, same game
	if p.DriftFrom(ReadBuild(build)) != Same {
		t.Fatal("a rebuild of the same game counts as a change")
	}
	os.WriteFile(filepath.Join(build, "game.json"), []byte(`{"speed":2}`), 0o644)
	if p.DriftFrom(ReadBuild(build)) != GameChanged {
		t.Fatal("a changed game doesn't count as a change")
	}
}

// Changing only how the game looks (a texture, a stylesheet) keeps a play
// replaying the same, but its frames are drawn anew.
func TestALookOnlyChangeIsTheLookNotTheGame(t *testing.T) {
	root := t.TempDir()
	build := filepath.Join(root, "build")
	os.MkdirAll(build, 0o755)
	os.WriteFile(filepath.Join(build, "game.json"), []byte(`{"speed":1}`), 0o644)
	os.WriteFile(filepath.Join(build, "hud.css"), []byte(`.hp { color: red }`), 0o644)
	dir, err := Create(root, ReadBuild(build), "test")
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":1}`), 0o644)
	p, _ := Load(dir)
	before := ReadBuild(build)
	os.MkdirAll(filepath.Join(build, "logs"), 0o755)
	os.WriteFile(filepath.Join(build, "logs", "engine.log"), []byte("a run"), 0o644)
	if ReadBuild(build) != before || p.DriftFrom(ReadBuild(build)) != Same {
		t.Error("a run's log counts as a change to the build")
	}
	os.WriteFile(filepath.Join(build, "hud.css"), []byte(`.hp { color: blue }`), 0o644)
	if after := ReadBuild(build); after.Game != before.Game || after.Look == before.Look || p.DriftFrom(after) != LookChanged {
		t.Errorf("a stylesheet change: %+v from %+v, drift %s", after, before, p.DriftFrom(after))
	}
}

func TestPlaysAreNeverCommitted(t *testing.T) {
	root := t.TempDir()
	if _, err := Create(root, Build{}, "test"); err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(filepath.Join(root, ".jm", ".gitignore"))
	if err != nil || string(raw) != "*\n" {
		t.Fatalf(".jm/.gitignore: %q, %v", raw, err)
	}
	os.WriteFile(filepath.Join(root, ".jm", ".gitignore"), []byte("plays/\n"), 0o644)
	a, _ := Create(root, Build{}, "test")
	b, _ := Create(root, Build{}, "test")
	if raw, _ := os.ReadFile(filepath.Join(root, ".jm", ".gitignore")); string(raw) != "plays/\n" {
		t.Errorf("a project's own .jm/.gitignore was replaced: %q", raw)
	}
	if a == b {
		t.Error("two plays started the same second share a folder")
	}
}

func TestAPlayReferenceThatIsntOneSaysSo(t *testing.T) {
	root := t.TempDir()
	writeSession(t, root, "2000-01-01_000000", `"markers":[]`)
	for _, ref := range []string{"latest-x", "-abc", "latest--1"} {
		if _, err := Find(root, ref); err == nil {
			t.Errorf("%q found a play", ref)
		}
	}
	if p, err := Find(root, "-0"); err != nil || p.ID != "2000-01-01_000000" {
		t.Errorf("-0: %v %v", p, err)
	}
}
