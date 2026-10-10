package main

import (
	"fmt"
	"os"
	"path/filepath"
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
