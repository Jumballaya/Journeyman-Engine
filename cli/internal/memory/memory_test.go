package memory

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestScaffoldMakesAVaultAndKeepsWhatsThere(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, Dir), 0o755)
	os.WriteFile(filepath.Join(root, Dir, "Pitch.md"), []byte("mine"), 0o644)
	made, err := Scaffold(root)
	if err != nil || len(made) != len(scaffold)-1 {
		t.Fatalf("made %v %v", made, err)
	}
	if data, _ := os.ReadFile(filepath.Join(root, Dir, "Pitch.md")); string(data) != "mine" {
		t.Errorf("the person's pitch was replaced: %q", data)
	}
	design, ok := Find(root, "design")
	if !ok || design.Type != "index" || strings.Join(design.Links, ",") != "Pitch,Pillars,Levels,Systems,Decisions,Notes" {
		t.Errorf("Design: %+v", design)
	}
	if again, _ := Scaffold(root); len(again) != 0 {
		t.Errorf("made again: %v", again)
	}
	for _, n := range Notes(root) {
		if strings.Contains(n.Path, ".obsidian") {
			t.Errorf("Obsidian's settings are a note: %s", n.Path)
		}
	}
}

func TestObserveAddsToTheNotesObservations(t *testing.T) {
	root := t.TempDir()
	n, err := Observe(root, "levels", "feel", "  1-1's   pit is unfair\nat the end ")
	if err != nil {
		t.Fatal(err)
	}
	n, _ = Observe(root, "Levels", "", "link [[Pillars]] and `scenes/main.scene.json`")
	want := "## Observations\n- [feel] 1-1's pit is unfair at the end\n- [note] link [[Pillars]] and `scenes/main.scene.json`\n\n## Relations\n- part_of [[Design]]\n"
	if !strings.HasSuffix(n.Text, want) {
		t.Errorf("Levels:\n%s", n.Text)
	}
	if len(n.Observations) != 2 || n.Observations[0] != (Observation{"feel", "1-1's pit is unfair at the end"}) || strings.Join(n.Links, ",") != "Pillars,Design" {
		t.Errorf("parsed: %+v", n)
	}
	if back := Backlinks(root, mustFind(t, root, "Pillars")); strings.Join(back, ",") != "Design,Levels" {
		t.Errorf("Pillars' backlinks: %v", back)
	}
	made, err := Observe(root, "Boss ideas", "idea", "a mirror boss")
	if err != nil || made.Path != "design/Boss ideas.md" || len(made.Observations) != 1 {
		t.Errorf("a new note: %+v %v", made, err)
	}
	if _, err := Observe(root, "Notes", "", "  "); err == nil {
		t.Error("an empty note was added")
	}
}

func TestDecideWritesADecisionNoteListedInDecisions(t *testing.T) {
	root := t.TempDir()
	d, err := Decide(root, "Jumps: variable height", "it serves [[Pillars]]: exact jumps")
	if err != nil {
		t.Fatal(err)
	}
	if d.Path != "design/decisions/Jumps variable height.md" || d.Type != "decision" || !strings.Contains(d.Text, "permalink: jumps-variable-height\n") {
		t.Errorf("decision: %+v\n%s", d, d.Text)
	}
	if index := mustFind(t, root, "Decisions"); !strings.Contains(strings.Join(index.Links, ","), "Jumps variable height") {
		t.Errorf("Decisions doesn't list it: %v", index.Links)
	}
	if _, err := Decide(root, "jumps variable height", "again"); err == nil {
		t.Error("decided twice")
	}
	if hits := Search(root, "SERVES [["); len(hits) != 2 || hits[0].Title != "Jumps variable height" || hits[0].Line == 0 {
		t.Errorf("search: %+v", hits)
	}
}

func mustFind(t *testing.T, root, name string) Note {
	t.Helper()
	n, ok := Find(root, name)
	if !ok {
		t.Fatalf("no note %s", name)
	}
	return n
}

func TestNotesFromElsewhereAreEditedSafely(t *testing.T) {
	root := t.TempDir()
	Observe(root, "Boss: ideas", "", "one")
	if n, _ := Observe(root, "Boss: ideas", "", "two"); len(n.Observations) != 2 {
		t.Errorf("a name made safe for files lost what was noted: %+v", n.Observations)
	}
	// Obsidian on Windows: CRLF; and a fenced example of a section.
	crlf := "---\r\ntitle: Game Feel\r\ntype: note\r\n---\r\n\r\n```\r\n## Observations\r\n```\r\n\r\n## Observations\r\n- [feel] a\r\n"
	os.WriteFile(filepath.Join(root, Dir, "feel.md"), []byte(crlf), 0o644)
	n, err := Observe(root, "Game Feel", "feel", "b")
	if err != nil || n.Path != "design/feel.md" || !strings.HasSuffix(n.Text, "```\n\n## Observations\n- [feel] a\n- [feel] b\n") {
		t.Errorf("feel.md: %v\n%s", err, n.Text)
	}
	outside := filepath.Join(t.TempDir(), "x.md")
	os.WriteFile(outside, []byte("mine"), 0o644)
	os.Remove(filepath.Join(root, Dir, "Notes.md"))
	os.Symlink(outside, filepath.Join(root, Dir, "Notes.md"))
	Observe(root, "Notes", "", "through the link")
	if data, _ := os.ReadFile(outside); string(data) != "mine" {
		t.Errorf("wrote through a link: %q", data)
	}
	if d, _ := Decide(root, "'til dawn", "why"); !strings.Contains(d.Text, "title: \"'til dawn\"\n") || d.Title != "'til dawn" {
		t.Errorf("title quoted: %s", d.Text)
	}
}
