package plays

import (
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// writePlay makes a play folder the way the engine does: 120 frames of 1/60 s,
// a marker at 90, samples every 30 frames with a score going up.
func writePlay(t *testing.T, root, id string) *Play {
	t.Helper()
	dir := filepath.Join(Root(root), id)
	os.MkdirAll(filepath.Join(dir, "thumbs"), 0o755)
	os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"game":"G","frames":120,"seconds":2,
		"markers":[{"n":1,"frame":90,"time":1.5,"scene":"scenes/b.scene.json","note":"here","image":"markers/1.png"}],"ended":"quit"}`), 0o644)
	frames := make([]byte, 120*4)
	for i := 0; i < 120; i++ {
		binary.LittleEndian.PutUint32(frames[i*4:], math.Float32bits(1.0/60))
	}
	os.WriteFile(filepath.Join(dir, "frames.bin"), frames, 0o644)
	os.WriteFile(filepath.Join(dir, "timeline.jsonl"), []byte(strings.Join([]string{
		`{"f":0,"t":0,"scene":"scenes/a.scene.json","entities":3,"session":{"score":0,"name":"x"}}`,
		`{"f":30,"t":0.5,"scene":"scenes/a.scene.json","entities":3,"session":{"score":10,"name":"x"}}`,
		`{"f":60,"t":1,"scene":"scenes/b.scene.json","entities":5,"session":{"score":10,"lives":2}}`,
		`{"f":90,"t":1.5,"scene":"scenes/b.scene.json","entities":5,"session":{"score":40,"lives":1,"deaths":1}}`,
	}, "\n")+"\n"), 0o644)
	for _, f := range []string{"000000.jpg", "000060.jpg"} {
		os.WriteFile(filepath.Join(dir, "thumbs", f), []byte("jpg"), 0o644)
	}
	p, err := Load(dir)
	if err != nil {
		t.Fatal(err)
	}
	return p
}

func TestMomentsResolveToFrames(t *testing.T) {
	p := writePlay(t, t.TempDir(), "2026-01-01_120000")
	for spec, want := range map[string]uint64{
		"": 119, "end": 119, "start": 0, "42": 42, "marker:1": 90, "m1": 90,
		"0.5s": 30, "1s": 60, "0:01": 60, "0:01.5": 90, "1.99s": 119,
	} {
		if got, err := p.FrameAt(spec); err != nil || got != want {
			t.Errorf("%q: got %d, %v; want %d", spec, got, err, want)
		}
	}
	for _, bad := range []string{"120", "m2", "soon", "5s"} {
		if _, err := p.FrameAt(bad); err == nil {
			t.Errorf("%q should be an error", bad)
		}
	}
}

func TestPlaysAreFoundByIdPrefixOrRecency(t *testing.T) {
	root := t.TempDir()
	writePlay(t, root, "2026-01-01_120000")
	writePlay(t, root, "2026-01-02_090000")
	writePlay(t, root, "2026-01-02_100000")
	for ref, want := range map[string]string{
		"": "2026-01-02_100000", "latest": "2026-01-02_100000", "latest-1": "2026-01-02_090000",
		"latest-2": "2026-01-01_120000", "2026-01-01": "2026-01-01_120000", "2026-01-02_09": "2026-01-02_090000",
	} {
		p, err := Find(root, ref)
		if err != nil || p.ID != want {
			t.Errorf("%q: got %v, %v; want %s", ref, p, err, want)
		}
	}
	if _, err := Find(root, "2026-01-02"); err == nil || !strings.Contains(err.Error(), "matches 2") {
		t.Errorf("an ambiguous prefix: %v", err)
	}
	if _, err := Find(t.TempDir(), ""); err == nil || !strings.Contains(err.Error(), "jm run") {
		t.Errorf("no plays says how to get one: %v", err)
	}
}

func TestASummaryTellsWhatHappened(t *testing.T) {
	p := writePlay(t, t.TempDir(), "2026-01-01_120000")
	s, err := p.Summarize()
	if err != nil {
		t.Fatal(err)
	}
	if len(s.Scenes) != 2 || s.Scenes[0].Scene != "scenes/a.scene.json" || s.Scenes[1].FromFrame != 60 || s.Scenes[1].To != 1.5 {
		t.Fatalf("scenes: %+v", s.Scenes)
	}
	if len(s.Values) != 3 {
		t.Fatalf("values: %+v", s.Values) // score and lives changed, deaths was set; name isn't a number
	}
	deaths, lives, score := s.Values[0], s.Values[1], s.Values[2]
	if deaths.Key != "deaths" || deaths.Since != 3 || deaths.First != 1 || deaths.Last != 1 {
		t.Fatalf("deaths (set once, at the last sample): %+v", deaths)
	}
	if score.Key != "score" || score.First != 0 || score.Last != 40 || len(score.Points) != 4 {
		t.Fatalf("score: %+v", score)
	}
	if lives.Key != "lives" || lives.Since != 2 || lives.Points[0] != 2 || lives.First != 2 || lives.Last != 1 || lives.Min != 1 {
		t.Fatalf("lives (set from the third sample on; not 0 before it): %+v", lives)
	}
	if len(s.Thumbs) != 2 || s.Thumbs[1].Frame != 60 || math.Abs(s.Thumbs[1].Time-1) > 1e-4 {
		t.Fatalf("thumbs: %+v", s.Thumbs)
	}
	if len(s.Markers) != 1 || s.Markers[0].Note != "here" {
		t.Fatalf("markers: %+v", s.Markers)
	}
}

// What can't be answered says why: a play with no frames, a time that isn't
// one, and a play folder whose session.json is damaged.
func TestBadMomentsAndPlaysSayWhy(t *testing.T) {
	root := t.TempDir()
	p := writePlay(t, root, "2026-01-01_120000")
	if _, err := p.FrameAt("1:99"); err == nil || !strings.Contains(err.Error(), "60 seconds") {
		t.Errorf("1:99: %v", err)
	}
	empty := &Play{ID: "e", Meta: Meta{Frames: 0}}
	if _, err := empty.FrameAt("end"); err == nil || !strings.Contains(err.Error(), "no frames") {
		t.Errorf("a play with no frames: %v", err)
	}
	broken := filepath.Join(Root(root), "2026-01-03_000000")
	os.MkdirAll(broken, 0o755)
	os.WriteFile(filepath.Join(broken, "session.json"), []byte(`{"format":1,"fra`), 0o644)
	if _, err := Find(root, "2026-01-03_000000"); err == nil || !strings.Contains(err.Error(), "session.json") {
		t.Errorf("a damaged play: %v", err)
	}
	if got, err := Find(root, "latest"); err != nil || got.ID != "2026-01-01_120000" {
		t.Errorf("a damaged play is skipped in the list: %v, %v", got, err)
	}
}

func TestPressesAreHoldsAroundAMoment(t *testing.T) {
	p := writePlay(t, t.TempDir(), "2026-01-01_120000")
	os.WriteFile(filepath.Join(p.Dir, "inputs.jsonl"), []byte(strings.Join([]string{
		`{"f":0,"type":"resize","w":10,"h":10}`,
		`{"f":6,"type":"key","name":"ArrowRight","down":true}`,
		`{"f":12,"type":"key","name":"ArrowRight","down":true}`, // a repeat
		`{"f":30,"type":"key","name":"Space","down":true}`,
		`{"f":54,"type":"key","name":"Space","down":false}`,
		`{"f":60,"type":"key","name":"ArrowRight","down":false}`,
		`{"f":70,"type":"key","name":"MouseLeft","down":true}`,
		`{"f":70,"type":"button","button":0,"down":false}`, // the same button, by number: held for frame 70
		`{"f":100,"type":"button","button":0,"down":true}`,
		`{"f":110,"type":"key","name":"Enter","down":true}`,
		`{"f":400,"type":"key","name":"Enter","down":false}`, // past the frames kept (a crash): the end
		`{"f":430,"type":"key","name":"Tab","down":true}`,    // all past them: no time at the end
		`{"f":435,"type":"key","name":"Tab","down":false}`,
	}, "\n")+"\n"), 0o644)
	got, err := p.Presses(50, 119)
	if err != nil {
		t.Fatal(err)
	}
	want := []Press{{Input: "ArrowRight", From: 0.1, To: 1}, {Input: "Space", From: 0.5, To: 0.9}, {Input: "MouseLeft", From: 1.167, To: 1.183},
		{Input: "MouseLeft", From: 1.667, To: 2, Open: true}, {Input: "Enter", From: 1.833, To: 2}}
	if len(got) != len(want) {
		t.Fatalf("got %+v, want %+v", got, want)
	}
	for i := range want {
		if g := got[i]; g.Input != want[i].Input || g.From != want[i].From || g.To != want[i].To || g.Open != want[i].Open {
			t.Errorf("press %d: got %+v, want %+v", i, g, want[i])
		}
	}
	if got, _ := p.Presses(120, 120); len(got) != 3 || got[2].Input != "Tab" || got[2].From != 2 || got[2].To != 2 {
		t.Errorf("a press after the last frame kept is at the end, not before it: %+v", got)
	}
	if got, _ := p.Presses(72, 99); len(got) != 0 {
		t.Errorf("nothing held between 72 and 99, got %+v", got)
	}
	if f := p.FrameBefore(90, LeadIn); f != 0 {
		t.Errorf("2 s before frame 90 is the start, got %d", f)
	}
	if f := p.FrameBefore(90, 0.5); f != 60 {
		t.Errorf("0.5 s before frame 90 is frame 60, got %d", f)
	}
	s, _ := p.Summarize()
	if len(s.Markers[0].Pressed) != 3 {
		t.Errorf("marker 1 (frame 90) follows ArrowRight, Space and a click: %+v", s.Markers[0].Pressed)
	}
}
