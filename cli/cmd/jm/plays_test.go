package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

func TestOldUnmarkedPlaysArePrunedMarkedOnesKept(t *testing.T) {
	root := t.TempDir()
	write := func(id, markers string) {
		dir := filepath.Join(plays.Root(root), id)
		os.MkdirAll(dir, 0o755)
		os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":1,"markers":`+markers+`}`), 0o644)
	}
	write("2000-01-01_000000", `[{"n":1,"frame":0}]`) // the oldest, but marked
	for i := 1; i <= keptPlays+5; i++ {
		write(fmt.Sprintf("2000-01-02_%06d", i), `[]`)
	}
	pruneOldPlays(root)
	all, _ := plays.List(root)
	if len(all) != keptPlays+1 {
		t.Fatalf("%d plays left, want %d", len(all), keptPlays+1)
	}
	if all[len(all)-1].ID != "2000-01-01_000000" || all[0].ID != fmt.Sprintf("2000-01-02_%06d", keptPlays+5) {
		t.Fatalf("kept the wrong ones: newest %s, oldest %s", all[0].ID, all[len(all)-1].ID)
	}
}

// An editor play has no fingerprint: the build it ran is the one still there
// when the build is older than the play. jm pins it then, so a rebuild of the
// same game isn't a change and a real change is.
func TestAnEditorPlaysBuildIsPinnedWhileUnchanged(t *testing.T) {
	root := t.TempDir()
	build := filepath.Join(root, "build")
	os.MkdirAll(build, 0o755)
	os.WriteFile(filepath.Join(build, archive.ManifestEntryKey), []byte(`{}`), 0o644)
	os.WriteFile(filepath.Join(build, "game.json"), []byte(`{"speed":1}`), 0o644)
	old := time.Now().Add(-time.Hour)
	os.Chtimes(filepath.Join(build, archive.ManifestEntryKey), old, old)
	dir := filepath.Join(plays.Root(root), "2000-01-01_000000")
	os.MkdirAll(dir, 0o755)
	started := time.Now().Add(-time.Minute).Format(time.RFC3339)
	os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":1,"ended":"quit","started":"`+started+`"}`), 0o644)
	p, err := plays.Load(dir)
	if err != nil {
		t.Fatal(err)
	}
	if gameChangedSince(root, p, buildFingerprint(build)) {
		t.Fatal("the build the play ran counts as a change")
	}
	if recordedBuild(p) != buildFingerprint(build) {
		t.Fatal("the build wasn't pinned to the play")
	}
	os.Chtimes(filepath.Join(build, archive.ManifestEntryKey), time.Now(), time.Now()) // rebuilt, same game
	if gameChangedSince(root, p, buildFingerprint(build)) {
		t.Fatal("a rebuild of the same game counts as a change")
	}
	os.WriteFile(filepath.Join(build, "game.json"), []byte(`{"speed":2}`), 0o644)
	if !gameChangedSince(root, p, buildFingerprint(build)) {
		t.Fatal("a changed game doesn't count as a change")
	}
}

func TestPlaysAreNeverCommitted(t *testing.T) {
	root := t.TempDir()
	newPlayDir(root)
	raw, err := os.ReadFile(filepath.Join(root, ".jm", ".gitignore"))
	if err != nil || string(raw) != "*\n" {
		t.Fatalf(".jm/.gitignore: %q, %v", raw, err)
	}
	os.WriteFile(filepath.Join(root, ".jm", ".gitignore"), []byte("plays/\n"), 0o644)
	newPlayDir(root)
	if raw, _ := os.ReadFile(filepath.Join(root, ".jm", ".gitignore")); string(raw) != "plays/\n" {
		t.Errorf("a project's own .jm/.gitignore was replaced: %q", raw)
	}
}

func TestAGamepadPlayIsntVerified(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, archive.ManifestEntryKey), []byte(`{"name":"g"}`), 0o644)
	dir := filepath.Join(plays.Root(root), "2000-01-01_000000")
	os.MkdirAll(dir, 0o755)
	os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":10,"gamepad":true}`), 0o644)
	t.Chdir(root)
	jsonOutput = true
	defer func() { jsonOutput = false }()
	var out strings.Builder
	if err := verifyPlay(&out, "latest"); err != nil {
		t.Fatal(err)
	}
	var got map[string]any
	json.Unmarshal([]byte(out.String()), &got)
	if same, ok := got["same"]; !ok || same != nil || got["reason"] != "played with a gamepad: not replayable" {
		t.Errorf("verify said %s", out.String())
	}
}

// Changing only how the game looks (a texture, a stylesheet) keeps a play
// replaying the same, but its replayed frames are drawn anew.
func TestALookOnlyChangeRedrawsFramesButKeepsThePlay(t *testing.T) {
	build := t.TempDir()
	os.WriteFile(filepath.Join(build, "game.json"), []byte(`{"speed":1}`), 0o644)
	os.WriteFile(filepath.Join(build, "hud.css"), []byte(`.hp { color: red }`), 0o644)
	game, look := buildFingerprint(build), lookFingerprint(build)
	os.WriteFile(filepath.Join(build, "hud.css"), []byte(`.hp { color: blue }`), 0o644)
	if buildFingerprint(build) != game {
		t.Error("a stylesheet change counts as a change to how the game plays")
	}
	if lookFingerprint(build) == look || frameCacheKey(lookFingerprint(build)) == frameCacheKey(look) {
		t.Error("a stylesheet change keeps the frames drawn before it")
	}
	look = lookFingerprint(build)
	os.MkdirAll(filepath.Join(build, "logs"), 0o755)
	os.WriteFile(filepath.Join(build, "logs", "engine.log"), []byte("a run"), 0o644)
	if lookFingerprint(build) != look {
		t.Error("a run's log counts as a change to the build")
	}
}
