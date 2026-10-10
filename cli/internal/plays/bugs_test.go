package plays

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

// A paused game's clock stops; a play's times are play time, the same as the
// moments a person or an agent gives.
func TestSampleTimesArePlayTimeNotTheGamesClock(t *testing.T) {
	p := writePlay(t, t.TempDir(), "2026-01-01_120000")
	os.WriteFile(filepath.Join(p.Dir, "timeline.jsonl"), []byte(
		`{"f":0,"t":0,"scene":"a","session":{}}`+"\n"+`{"f":90,"t":0.2,"scene":"b","session":{}}`+"\n"), 0o644)
	s, _ := p.Summarize()
	if len(s.Scenes) != 2 || s.Scenes[1].From != 1.5 {
		t.Errorf("scene b from %v; frame 90 is at 1.5 s of play", s.Scenes)
	}
}

// A play cut short (a crash mid-write) has fewer frames recorded than its
// session.json says: moments past them aren't in it.
func TestMomentsStayInWhatWasRecorded(t *testing.T) {
	p := writePlay(t, t.TempDir(), "2026-01-01_120000")
	data, _ := os.ReadFile(filepath.Join(p.Dir, "frames.bin"))
	os.WriteFile(filepath.Join(p.Dir, "frames.bin"), data[:60*4], 0o644)
	p, _ = Load(p.Dir)
	if f, err := p.FrameAt("end"); err != nil || f != 59 {
		t.Errorf("end: %d, %v; want 59", f, err)
	}
	for _, spec := range []string{"m1", "1.5s", "100"} {
		if f, err := p.FrameAt(spec); err == nil {
			t.Errorf("%q: frame %d of a 60-frame recording", spec, f)
		}
	}
}

func TestAPlaysOwnLengthIsItsLastFrame(t *testing.T) {
	p := writePlay(t, t.TempDir(), "2026-01-01_120000")
	for _, spec := range []string{"2s", "0:02.0"} {
		if f, err := p.FrameAt(spec); err != nil || f != 119 {
			t.Errorf("%q: %d, %v; want 119", spec, f, err)
		}
	}
	if _, err := p.FrameAt("2.5s"); err == nil {
		t.Error("a time past the play's end is a moment in it")
	}
}

func TestCorruptFrameNumbersDontPanic(t *testing.T) {
	p := writePlay(t, t.TempDir(), "2026-01-01_120000")
	os.WriteFile(filepath.Join(p.Dir, "thumbs", "18446744073709551615.jpg"), []byte("jpg"), 0o644)
	p.Meta.Markers[0].Frame = 18446744073709551615
	if _, err := p.FrameAt("m1"); err == nil {
		t.Error("a marker past the play's end is a moment in it")
	}
	if _, err := p.Summarize(); err != nil {
		t.Error(err)
	}
}

func TestThumbsAndPlaysAreInOrder(t *testing.T) {
	root := t.TempDir()
	p := writePlay(t, root, "2026-01-01_120000")
	os.WriteFile(filepath.Join(p.Dir, "thumbs", "1000020.jpg"), []byte("jpg"), 0o644)
	os.WriteFile(filepath.Join(p.Dir, "thumbs", "999960.jpg"), []byte("jpg"), 0o644)
	thumbs := p.Thumbs()
	if thumbs[len(thumbs)-2].Frame != 999960 || thumbs[len(thumbs)-1].Frame != 1000020 {
		t.Errorf("thumbs out of order: %v", thumbs)
	}
	writePlay(t, root, "2026-01-01_120000_2")
	writePlay(t, root, "2026-01-01_120000_10")
	if latest, _ := Find(root, "latest"); latest.ID != "2026-01-01_120000_10" {
		t.Errorf("latest is %s", latest.ID)
	}
}

func TestAPlayStillBeingRecordedIsntPruned(t *testing.T) {
	root := t.TempDir()
	running := writePlay(t, root, "2026-01-01_120000")
	os.WriteFile(filepath.Join(running.Dir, "session.json"), []byte(`{"format":1,"frames":1,"ended":"running"}`), 0o644)
	unstarted := filepath.Join(Root(root), "2026-01-01_110000")
	os.MkdirAll(unstarted, 0o755)
	old := time.Now().Add(-2 * time.Hour)
	os.Chtimes(unstarted, old, old)
	if removed, _ := Prune(root, 0, false); removed != 0 {
		t.Errorf("removed %d", removed)
	}
	if !exists(running.Dir) || exists(unstarted) {
		t.Errorf("running kept: %v; unstarted gone: %v", exists(running.Dir), !exists(unstarted))
	}
}

// An editor play recorded, then the scene edited (the editor writes it into
// the build) before anything looked: the edit isn't the play's build.
func TestAnEditAfterAnEditorPlayIsAChange(t *testing.T) {
	root := t.TempDir()
	build := filepath.Join(root, "build")
	os.MkdirAll(build, 0o755)
	os.WriteFile(filepath.Join(build, ".jm.json"), []byte(`{}`), 0o644)
	started := time.Now().Add(-time.Minute)
	p, _ := Load(writeSession(t, root, "2000-01-01_000000", `"ended":"quit","started":"`+started.UTC().Format(time.RFC3339)+`"`))
	old := started.Add(-time.Hour)
	os.Chtimes(filepath.Join(build, ".jm.json"), old, old)
	os.WriteFile(filepath.Join(build, "level.scene.json"), []byte(`{"edited":true}`), 0o644)
	if p.DriftFrom(ReadBuild(build)) != GameChanged || p.Info().Build != "" {
		t.Error("an edit after the play was pinned as its build")
	}
}
