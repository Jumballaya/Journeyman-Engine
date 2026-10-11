package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

func TestAGamepadPlayIsntVerified(t *testing.T) {
	root := t.TempDir()
	dir := filepath.Join(plays.Root(root), "2000-01-01_000000")
	os.MkdirAll(dir, 0o755)
	os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":10,"gamepad":true}`), 0o644)
	p, _ := plays.Load(dir)
	v, err := verify(root, p, plays.Build{})
	if err != nil || v.Same != nil || v.Reason != "played with a gamepad: not replayable" {
		t.Errorf("verify said %+v, %v", v, err)
	}
}

// A play replays with the project's build: running any other one isn't recorded.
func TestOnlyTheProjectsOwnBuildIsRecorded(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, archive.ManifestEntryKey), []byte(`{}`), 0o644)
	for _, c := range []struct {
		target string
		record bool
	}{{"build", true}, {"other-build", false}} {
		got, ok := recordingProject(gameToRun{target: filepath.Join(root, c.target), kind: "build"}, runOptions{})
		if ok != c.record || (ok && got != root) {
			t.Errorf("%s: recorded=%v in %q", c.target, ok, got)
		}
	}
}

// The listing says when each marker is, so an agent can pick one without opening the play.
func TestPlaysListTheirMarkers(t *testing.T) {
	root := t.TempDir()
	dir := filepath.Join(plays.Root(root), "2000-01-01_000000")
	os.MkdirAll(dir, 0o755)
	os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":300,"markers":[
		{"n":1,"frame":180,"time":3,"scene":"s"},{"n":2,"frame":240,"time":4,"scene":"s","note":"too high"}]}`), 0o644)
	listing, err := listPlays(root)
	if err != nil || len(listing.Plays) != 1 {
		t.Fatalf("listed %+v, %v", listing, err)
	}
	if got := strings.Join(listing.Plays[0].Marks, "|"); got != "m1 0:03.0|m2 0:04.0: too high" {
		t.Errorf("marks %q", got)
	}
}
