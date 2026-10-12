package main

import (
	"encoding/json"
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

// What moved leads; values the game set once and left say so on one line.
func TestPlayShowLeadsWithValuesThatChanged(t *testing.T) {
	o := playOverview{Summary: plays.Summary{ID: "p", SampleT: []float64{0, 0.5, 1, 1.5}, Values: []plays.Series{
		{Key: "cartArrived", Since: 1, First: 0, Last: 0, Min: 0, Max: 0},
		{Key: "lives", First: 3, Last: 2, Min: 2, Max: 3},
		{Key: "deaths", Since: 3, First: 1, Last: 1, Min: 1, Max: 1},
	}}}
	text := o.text()
	if !strings.Contains(text, "  lives: 3 → 2\n  set, then never changed: cartArrived 0 (0:00.5), deaths 1 (0:01.5)\n") {
		t.Errorf("got:\n%s", text)
	}
}

// A play that goes differently says from when, and how its end differs, in words.
func TestVerifySaysWhenAndHowAPlayGoesDifferently(t *testing.T) {
	differs, same := uint64(90), false
	v := verifyResult{Play: "p", Frames: 600, Same: &same, DiffersBy: &differs, DiffersAt: 1.5, Drift: plays.GameChanged, CheckedTo: 570,
		Changed: changedValues(
			map[string]json.RawMessage{"lives": json.RawMessage(`2`), "shards": json.RawMessage(`3`), "won": json.RawMessage(`false`), "big": json.RawMessage(`9007199254740993`)},
			map[string]json.RawMessage{"lives": json.RawMessage(`3`), "shards": json.RawMessage(`3`), "won": json.RawMessage(` false`), "boss": json.RawMessage(`1`), "big": json.RawMessage(`9007199254740992`)})}
	want := "p goes differently now, by 0:01.5 (frame 90): the game changed since it was played\n" +
		"at frame 570 (its timeline's end) it differs: big 9007199254740993 → 9007199254740992, boss unset → 1, lives 2 → 3\n"
	if got := v.text(); got != want {
		t.Errorf("got:\n%s\nwant:\n%s", got, want)
	}
	v.Changed = []valueChange{}
	if got := v.text(); !strings.Contains(got, "the session values match the play's") {
		t.Errorf("nothing changed at the end:\n%s", got)
	}
}
