package memory

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"sync"
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

// Changes from several jm at once all land.
func TestOverlappingChangesAllLand(t *testing.T) {
	root := t.TempDir()
	var wg sync.WaitGroup
	for i := range 16 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if _, err := Observe(root, "Notes", "idea", fmt.Sprint("idea ", i)); err != nil {
				t.Error(err)
			}
		}()
	}
	for i := range 8 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if _, err := Decide(root, fmt.Sprint("Choice ", i), "because"); err != nil {
				t.Error(err)
			}
		}()
	}
	wg.Wait()
	if n, _ := Find(root, "Notes"); len(n.Observations) != 16 {
		t.Errorf("%d of 16 observations kept", len(n.Observations))
	}
	if index, _ := Find(root, "Decisions"); len(index.Links) != 9 { // Design, and the 8
		t.Errorf("the index links %v", index.Links)
	}
	wins := 0
	var mu sync.Mutex
	for range 2 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if _, err := Decide(root, "Same", "once"); err == nil {
				mu.Lock()
				wins++
				mu.Unlock()
			}
		}()
	}
	wg.Wait()
	if wins != 1 {
		t.Errorf("%d decisions of one title", wins)
	}
}

// A design folder linked out of the game is refused before anything is made there.
func TestALinkedDesignFolderGetsNothing(t *testing.T) {
	root, outside := t.TempDir(), t.TempDir()
	for _, f := range scaffold { // all there but Obsidian's settings, which would make a folder
		if !strings.HasPrefix(f.path, ".obsidian") {
			os.WriteFile(filepath.Join(outside, f.path), []byte(f.text), 0o644)
		}
	}
	if os.Symlink(outside, filepath.Join(root, Dir)) != nil {
		t.Skip("no symlinks here")
	}
	if _, err := Scaffold(root); err == nil {
		t.Error("scaffolded through the link")
	}
	if _, err := Observe(root, "Notes", "", "x"); err == nil {
		t.Error("noted through the link")
	}
	if _, err := Decide(root, "X", "y"); err == nil {
		t.Error("decided through the link")
	}
	if entries, _ := os.ReadDir(outside); len(entries) != len(scaffold)-2 {
		t.Errorf("made outside the game: %v", entries)
	}
}

// Code in a note is an example: no observations or links come from it.
func TestCodeInANoteIsntMemory(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	text := "# Howto\n\n```md\n- [fact] fenced\n[[Pillars]]\n```\n~~~~\n- [fact] tilde\n```\n[[Pillars]]\n~~~~\n" +
		"Inline `[[Pillars]]` and ``- [x] [[Pillars]]``, and `across\n[[Pillars]]` lines.\n\n" +
		"    ```\n\n```js `x`\n\n- [fact] literal ` here\n- [fact] `Player` is real, in `scenes/a.scene.json`\n- [fact] another ` here\n" +
		"- see [[Systems]] and \\`[[Levels]]\\`\n"
	os.WriteFile(filepath.Join(root, Dir, "Howto.md"), []byte(text), 0o644)
	n, _ := Find(root, "Howto")
	if len(n.Observations) != 3 || n.Observations[1].Text != "`Player` is real, in `scenes/a.scene.json`" || strings.Join(n.Links, ",") != "Systems,Levels" {
		t.Errorf("observations %v, links %v", n.Observations, n.Links)
	}
	pillars, _ := Find(root, "Pillars")
	if slices.Contains(Backlinks(root, pillars), "Howto") {
		t.Error("Pillars is linked from code only")
	}
}

// A YAML title in single quotes is its text: ” is one '.
func TestASingleQuotedTitleIsFound(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	os.WriteFile(filepath.Join(root, Dir, "rock.md"), []byte("---\ntitle: 'Rock ''n'' roll'\n---\n\n# Rock\n"), 0o644)
	if _, err := Observe(root, "Rock 'n' roll", "", "loud"); err != nil {
		t.Fatal(err)
	}
	n, ok := Find(root, "Rock 'n' roll")
	if !ok || n.Path != Dir+"/rock.md" || len(n.Observations) != 1 {
		t.Errorf("found %v %+v", ok, n)
	}
	if len(Notes(root)) != len(scaffold)-2+1 {
		t.Errorf("another note was made: %d notes", len(Notes(root)))
	}
}

// A decision whose index couldn't be updated says so, and deciding again finishes it.
func TestADecisionThatCouldntBeIndexedIsFinishedByDecidingAgain(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("Windows doesn't keep a read-only folder's files")
	}
	root := t.TempDir()
	Scaffold(root)
	index := filepath.Join(root, Dir, "Decisions.md")
	before, _ := os.ReadFile(index)
	os.MkdirAll(filepath.Join(root, Dir, "decisions"), 0o755)
	os.Chmod(filepath.Join(root, Dir), 0o555) // the index can't be replaced; decisions/ still takes notes
	_, err := Decide(root, "Wide", "room")
	os.Chmod(filepath.Join(root, Dir), 0o755)
	if err == nil || !strings.Contains(err.Error(), "Decisions") {
		t.Errorf("a decision missing from the index: %v", err)
	}
	if _, err := Decide(root, "Wide", "room"); err != nil {
		t.Fatalf("deciding again: %v", err)
	}
	after, _ := os.ReadFile(index)
	if strings.Count(string(after), "[[Wide]]") != 1 || !strings.HasPrefix(string(after), strings.TrimRight(string(before), "\n")) {
		t.Errorf("index:\n%s", after)
	}
	if _, err := Decide(root, "Wide", "room"); err == nil {
		t.Error("decided a third time")
	}
}

// TestMain keeps the lock memory changes hold (beside the editor's settings) out of the person's.
func TestMain(m *testing.M) {
	dir, _ := os.MkdirTemp("", "memory-settings")
	for _, k := range []string{"HOME", "APPDATA", "XDG_DATA_HOME"} {
		os.Setenv(k, dir)
	}
	code := m.Run()
	os.RemoveAll(dir)
	os.Exit(code)
}

// design/ linked to a folder in the game is the notes: read and added to there.
func TestADesignLinkInTheGameIsTheNotes(t *testing.T) {
	root := t.TempDir()
	real := filepath.Join(root, "docs", "design")
	os.MkdirAll(real, 0o755)
	os.WriteFile(filepath.Join(real, "Notes.md"), []byte("---\ntitle: Notes\n---\n\n# Notes\n\nmine\n"), 0o644)
	if os.Symlink(filepath.Join("docs", "design"), filepath.Join(root, Dir)) != nil {
		t.Skip("no symlinks here")
	}
	for _, text := range []string{"first", "second"} {
		if _, err := Observe(root, "Notes", "", text); err != nil {
			t.Fatal(err)
		}
	}
	data, _ := os.ReadFile(filepath.Join(real, "Notes.md"))
	if !strings.Contains(string(data), "mine") || !strings.Contains(string(data), "- [note] first") || !strings.Contains(string(data), "- [note] second") {
		t.Errorf("Notes.md:\n%s", data)
	}
	if n, ok := Find(root, "Notes"); !ok || n.Path != Dir+"/Notes.md" || len(Notes(root)) < 2 {
		t.Errorf("found %v %+v; %d notes", ok, n, len(Notes(root)))
	}
}

// Indented code is an example too; a list's indented lines aren't code.
func TestIndentedCodeIsntMemory(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	text := "# Ex\n\nLike this:\n\n    - [fact] example\n    see [[Pillars]]\n\n\t- [fact] tabbed\n\n- [fact] item\n\n    - [fact] continued\n\n- [fact] real\n"
	os.WriteFile(filepath.Join(root, Dir, "Ex.md"), []byte(text), 0o644)
	n, _ := Find(root, "Ex")
	var got []string
	for _, o := range n.Observations {
		got = append(got, o.Text)
	}
	if strings.Join(got, ",") != "item,continued,real" || len(n.Links) != 0 {
		t.Errorf("observations %v, links %v", got, n.Links)
	}
}

// A YAML comment after a title isn't the title.
func TestATitlesCommentIsntItsTitle(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	for file, title := range map[string]string{"a.md": `"Rock" # c`, "b.md": `'It''s' # c`, "c.md": "Plain # c", "d.md": "C#"} {
		os.WriteFile(filepath.Join(root, Dir, file), []byte("---\ntitle: "+title+"\n---\n\n# x\n"), 0o644)
	}
	for name, file := range map[string]string{"Rock": "a.md", "It's": "b.md", "Plain": "c.md", "C#": "d.md"} {
		if n, ok := Find(root, name); !ok || n.Path != Dir+"/"+file {
			t.Errorf("%s: %v %+v", name, ok, n)
		}
	}
	before := len(Notes(root))
	if _, err := Observe(root, "It's", "", "x"); err != nil || len(Notes(root)) != before {
		t.Errorf("observe made another note: %v", err)
	}
}

// A design/ link to a hidden folder in the game is the notes too, and an absolute one read from ".".
func TestADesignLinkToAHiddenFolderIsTheNotes(t *testing.T) {
	root := t.TempDir()
	real := filepath.Join(root, ".notes-real")
	os.MkdirAll(real, 0o755)
	os.WriteFile(filepath.Join(real, "Notes.md"), []byte("---\ntitle: Notes\n---\n\n# Notes\n\nmine\n"), 0o644)
	if os.Symlink(real, filepath.Join(root, Dir)) != nil { // absolute
		t.Skip("no symlinks here")
	}
	for _, text := range []string{"first", "second"} {
		if _, err := Observe(root, "Notes", "", text); err != nil {
			t.Fatal(err)
		}
	}
	data, _ := os.ReadFile(filepath.Join(real, "Notes.md"))
	if !strings.Contains(string(data), "mine") || !strings.Contains(string(data), "first") || !strings.Contains(string(data), "second") {
		t.Errorf("Notes.md:\n%s", data)
	}
	t.Chdir(root)
	if len(Notes(".")) < 2 {
		t.Errorf("notes from \".\": %v", Notes("."))
	}
}

// A list in a fence is an example: it doesn't make the indented code after it a list's.
func TestAFencedListDoesntMakeALaterCodeBlockAList(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	text := "# Ex\n\n```\n- [example] in the fence\n```\n\n    - [fact] code after it\n    [[Pillars]]\n\n- [fact] real\n"
	os.WriteFile(filepath.Join(root, Dir, "Ex.md"), []byte(text), 0o644)
	n, _ := Find(root, "Ex")
	if len(n.Observations) != 1 || n.Observations[0].Text != "real" || len(n.Links) != 0 {
		t.Errorf("observations %v, links %v", n.Observations, n.Links)
	}
}

// A comment after a tab is a comment too.
func TestATabbedTitleCommentIsntTheTitle(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	os.WriteFile(filepath.Join(root, Dir, "tab.md"), []byte("---\ntitle: Tab title\t# comment\n---\n\n# x\n"), 0o644)
	os.WriteFile(filepath.Join(root, Dir, "csharp.md"), []byte("---\ntitle: C# # language\n---\n\n# x\n"), 0o644)
	if n, ok := Find(root, "C#"); !ok || n.Path != Dir+"/csharp.md" {
		t.Errorf("C#: %v %+v", ok, n)
	}
	before := len(Notes(root))
	if _, err := Observe(root, "Tab title", "", "x"); err != nil || len(Notes(root)) != before {
		t.Errorf("observe made another note: %v", err)
	}
	if n, ok := Find(root, "Tab title"); !ok || n.Path != Dir+"/tab.md" {
		t.Errorf("found %v %+v", ok, n)
	}
}

// A note that can't be read isn't taken for missing: nothing is written over it.
func TestAnUnreadableNoteIsntReplaced(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	notes := filepath.Join(root, Dir, "Notes.md")
	before, _ := os.ReadFile(notes)
	os.Chmod(notes, 0o000)
	defer os.Chmod(notes, 0o644)
	if _, err := os.ReadFile(notes); err == nil {
		t.Skip("readable anyway (root?)")
	}
	if _, err := Observe(root, "Notes", "", "x"); err == nil {
		t.Error("noted over an unreadable note")
	}
	if _, err := Decide(root, "Wide", "why"); err == nil {
		t.Error("decided with a note unread")
	}
	os.Chmod(notes, 0o644)
	if after, _ := os.ReadFile(notes); string(after) != string(before) {
		t.Errorf("Notes.md changed:\n%s", after)
	}
	if n, err := Observe(root, "Notes", "", "x"); err != nil || len(n.Observations) != 1 {
		t.Errorf("readable again: %v %+v", err, n.Observations)
	}
}

// A fence inside a list item doesn't end the list: what follows in the item still counts.
func TestAFenceInAListItemKeepsTheList(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	text := "# Ex\n\n- an item\n\n  ```\n  - [example] in the fence\n  ```\n\n    - [fact] nested after it\n    see [[Pillars]]\n"
	os.WriteFile(filepath.Join(root, Dir, "Ex.md"), []byte(text), 0o644)
	n, _ := Find(root, "Ex")
	if len(n.Observations) != 1 || n.Observations[0].Text != "nested after it" || strings.Join(n.Links, ",") != "Pillars" {
		t.Errorf("observations %v, links %v", n.Observations, n.Links)
	}
}
