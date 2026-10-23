package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math"
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
	v, err := verify(nil, root, p, plays.Build{})
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

// Play review opens from a chat's tabs ({} as its arguments): the latest play.
func TestPlayReviewOpensAsAThreadEntrypoint(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, archive.ManifestEntryKey), []byte(`{}`), 0o644)
	for _, id := range []string{"2000-01-01_000000", "2000-01-02_000000"} {
		dir := filepath.Join(plays.Root(root), id)
		os.MkdirAll(dir, 0o755)
		os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":60,"seconds":1}`), 0o644)
	}
	t.Chdir(root)
	replies := mcpSession(t, `{"jsonrpc":"2.0","id":1,"method":"tools/list"}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"play_show","arguments":{}}}`)
	for _, tool := range replies[1]["result"].(map[string]any)["tools"].([]any) {
		if tool := tool.(map[string]any); tool["name"] == "play_show" {
			meta := tool["_meta"].(map[string]any)
			if got := jsonOf(meta["openai/ui"]); tool["title"] != "Play review" || got != `{"entrypoints":[{"type":"thread"}]}` {
				t.Errorf("play_show: %v %s", tool["title"], got)
			}
		}
	}
	result := replies[2]["result"].(map[string]any)
	if result["isError"] == true || result["structuredContent"].(map[string]any)["id"] != "2000-01-02_000000" {
		t.Errorf("play_show {}: %v", result)
	}
}

func TestResumingAChangedGameSaysTheSpotMayDiffer(t *testing.T) {
	p := &plays.Play{ID: "p", Meta: plays.Meta{Markers: []plays.Marker{{N: 2, Frame: 90}}}}
	if text := resumedText(p, 90, false); strings.Contains(text, "changed") {
		t.Errorf("an unchanged game warns: %s", text)
	}
	if text := resumedText(p, 90, true); !strings.Contains(text, "marker 2's scene") || !strings.Contains(text, "may not be quite their spot") {
		t.Errorf("a changed game at a marker doesn't say where they land: %s", text)
	}
	if text := resumedText(p, 91, true); strings.Contains(text, "marker 2's scene") || !strings.Contains(text, "may not be quite their spot") {
		t.Errorf("a changed game off a marker: %s", text)
	}
}

// play_show at a moment opens there; a moment the play doesn't have is an error, not the first marker.
func TestPlayShowOpensAtTheMomentAsked(t *testing.T) {
	root := t.TempDir()
	recordedPlay(t, root, "2000-01-01_000000", 300, "", 180, 240)
	r := playShow(root, toolArgs{"at": "m2"})
	if o, ok := r.Structured.(playOverview); r.Failed || !ok || o.At == nil || *o.At != 240 {
		t.Fatalf("at m2: %+v", r)
	}
	if r := playShow(root, toolArgs{}); r.Structured.(playOverview).At != nil {
		t.Error("with no at, the widget picks the moment")
	}
	if r := playShow(root, toolArgs{"at": "m9"}); !r.Failed {
		t.Errorf("a marker the play doesn't have: %+v", r.Text)
	}
}

// recordedPlay writes a play of frames frames at 60 a second, by (plays.Agent
// or "" for the person), with a marker at each of markers.
func recordedPlay(t *testing.T, root, id string, frames int, by string, markers ...uint64) {
	t.Helper()
	dir := filepath.Join(plays.Root(root), id)
	os.MkdirAll(dir, 0o755)
	var marks []string
	for i, f := range markers {
		marks = append(marks, fmt.Sprintf(`{"n":%d,"frame":%d,"time":%g,"scene":"s"}`, i+1, f, float64(f)/60))
	}
	os.WriteFile(filepath.Join(dir, "session.json"), fmt.Appendf(nil, `{"format":1,"frames":%d,"markers":[%s]}`, frames, strings.Join(marks, ",")), 0o644)
	data := make([]byte, frames*4)
	for i := range frames {
		binary.LittleEndian.PutUint32(data[i*4:], math.Float32bits(1.0/60))
	}
	os.WriteFile(filepath.Join(dir, "frames.bin"), data, 0o644)
	os.WriteFile(filepath.Join(dir, "jm.json"), fmt.Appendf(nil, `{"by":%q}`, by), 0o644)
}

// A comparison looks where the person meant: their latest play's last marker.
func TestACompareLooksAtThePersonsLastMarker(t *testing.T) {
	root := t.TempDir()
	if m, err := compareMoment(root, "", ""); err != nil || m.Play != nil || m.Frame != 120 || m.At != "120" {
		t.Errorf("no plays: %+v %v", m, err)
	}
	recordedPlay(t, root, "2000-01-01_000000", 300, "", 90, 200)
	recordedPlay(t, root, "2000-01-02_000000", 300, plays.Agent, 50)
	for _, c := range []struct {
		play, at string
		id       string
		frame    uint64
		named    string
	}{
		{"", "", "2000-01-01_000000", 200, "m2"},  // the person's, not the agent's newer run; its last marker
		{"", "m1", "2000-01-01_000000", 90, "m1"}, // another moment of it
		{"", "start", "2000-01-01_000000", 0, "start"},
		{"2000-01-02_000000", "", "2000-01-02_000000", 50, "m1"},
		{"none", "", "", 120, "120"},
		{"none", "45", "", 45, "45"},
	} {
		m, err := compareMoment(root, c.play, c.at)
		id := ""
		if m.Play != nil {
			id = m.Play.ID
		}
		if err != nil || id != c.id || m.Frame != c.frame || m.At != c.named {
			t.Errorf("%q %q: %s %+v %v; want %s %d %s", c.play, c.at, id, m, err, c.id, c.frame, c.named)
		}
	}
	if _, err := compareMoment(root, "none", "m1"); err == nil || !strings.Contains(err.Error(), "at is a frame") {
		t.Errorf("a marker with no play: %v", err)
	}
	// Only an agent's runs: not played yet. Plays that can't be read aren't that.
	agentOnly := t.TempDir()
	recordedPlay(t, agentOnly, "2000-01-02_000000", 300, plays.Agent, 50)
	if m, err := compareMoment(agentOnly, "", ""); err != nil || m.Play != nil || m.Frame != 120 {
		t.Errorf("only an agent's run: %+v %v", m, err)
	}
	broken := t.TempDir()
	os.MkdirAll(filepath.Dir(plays.Root(broken)), 0o755)
	os.WriteFile(plays.Root(broken), []byte("not a folder"), 0o644)
	if _, err := compareMoment(broken, "", ""); err == nil {
		t.Error("an unreadable plays folder compared with no inputs, as if never played")
	}
}
