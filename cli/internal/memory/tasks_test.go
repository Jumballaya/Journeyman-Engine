package memory

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

func boardFile(t *testing.T, root string) string {
	t.Helper()
	data, _ := os.ReadFile(filepath.Join(root, "design", "Tasks.md"))
	return string(data)
}

func TestTheTaskBoard(t *testing.T) {
	root := t.TempDir()
	if _, got, err := Tasks(root); err != nil || len(got) != 0 {
		t.Errorf("no board: %v", got)
	}
	a, err := AddTask(root, "Fix the pit in 1-1", "", []string{"Levels", "scenes/main.scene.json", "jm://play/20261015-101500/m2"})
	if err != nil || a.ID != "t1" || a.Column != "To do" {
		t.Fatalf("add: %+v %v", a, err)
	}
	if strings.Join(a.Links, "|") != "[[Levels]]|scenes/main.scene.json|jm://play/20261015-101500/m2" {
		t.Errorf("links: %v", a.Links)
	}
	b, _ := AddTask(root, "Variable jump", "doing", nil)
	if _, err := MoveTask(root, "t1", "Done"); err != nil {
		t.Fatal(err)
	}
	if _, err := AddTask(root, "x", "Later", nil); err == nil {
		t.Error("a column that isn't")
	}
	want := "## To do\n\n## Doing\n- [ ] Variable jump ^t2\n\n## Done\n- [x] Fix the pit in 1-1 · [[Levels]] · `scenes/main.scene.json` · jm://play/20261015-101500/m2 ^t1\n"
	if !strings.HasSuffix(boardFile(t, root), want) || b.ID != "t2" {
		t.Errorf("Tasks.md:\n%s", boardFile(t, root))
	}
}

// By hand in Obsidian: prose, a column of their own, a task without an id, a
// copied id, a tick, a note under a task. A change keeps all of it.
func TestTheBoardKeepsHandEdits(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, "design"), 0o755)
	hand := "---\ntitle: Tasks\ncssclass: kanban\n---\n\n# Tasks\n\nMy own words.\n\n## To do\n- [ ] Coins respawn ^t4\n  - after a death\n\n  - and a blank line\n- [x] Title screen\n- [ ] Copy ^t4\n\n## Later\n- [ ] Boss\n"
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte(hand), 0o644)
	columns, tasks, _ := Tasks(root)
	if strings.Join(columns, ",") != "To do,Later,Doing,Done" || len(tasks) != 4 || tasks[1].Column != "Done" || tasks[0].Notes[0] != "  - after a death" {
		t.Fatalf("read: %v %+v", columns, tasks)
	}
	if tasks[2].ID == "t4" || tasks[1].ID == "" {
		t.Errorf("ids: %+v", tasks)
	}
	if _, err := MoveTask(root, "t4", "Doing"); err != nil {
		t.Fatal(err)
	}
	got := boardFile(t, root)
	for _, kept := range []string{"cssclass: kanban\n", "My own words.\n", "## Later\n- [ ] Boss ^", "\n## Doing\n- [ ] Coins respawn ^t4\n  - after a death\n\n  - and a blank line\n"} {
		if !strings.Contains(got, kept) {
			t.Errorf("lost %q:\n%s", kept, got)
		}
	}
	RemoveTask(root, "nope")
	if boardFile(t, root) != got {
		t.Error("removing a task that isn't changed the file")
	}
	if n, ok := Find(root, "Tasks"); !ok || n.Title != "Tasks" {
		t.Errorf("the board isn't a note: %+v", n)
	}
}

// A fence (``` or ~~~, closed only by its own kind and length) is code, not the board.
func TestFencedLinesAreNotTheBoard(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, "design"), 0o755)
	hand := "## To do\n- [ ] Coins ^t1\n\n ```\nits code\n ```\n~~~~\n```\n## Done\n- [ ] Not a task ^t9\n~~~~\n\n## Doing\n"
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte(hand), 0o644)
	if columns, tasks, _ := Tasks(root); strings.Join(columns, ",") != "To do,Doing,Done" || len(tasks) != 1 {
		t.Fatalf("read: %v %+v", columns, tasks)
	}
	if _, err := MoveTask(root, "t1", "Done"); err != nil {
		t.Fatal(err)
	}
	if got := boardFile(t, root); !strings.Contains(got, "```\n## Done\n- [ ] Not a task ^t9\n~~~~\n") || !strings.HasSuffix(got, "\n## Done\n- [x] Coins ^t1\n\n ```\nits code\n ```\n") || !strings.HasPrefix(got, "---\nlastTask: t1\n---\n\n## To do\n~~~~\n") {
		t.Errorf("Tasks.md:\n%s", got)
	}
}

// A board that can't be read isn't a new one: nothing is written over it.
func TestAnUnreadableBoardIsAnError(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, "design", "Tasks.md"), 0o755) // a folder: reading fails, not missing
	if _, _, err := Tasks(root); err == nil {
		t.Error("Tasks read it")
	}
	if _, err := AddTask(root, "Coins", "", nil); err == nil {
		t.Error("AddTask wrote over it")
	}
}

// Tasks added by several jm at once all land, each with its own id.
func TestOverlappingTasksAllLand(t *testing.T) {
	root := t.TempDir()
	var wg sync.WaitGroup
	for i := range 12 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if _, err := AddTask(root, fmt.Sprint("task ", i), "", nil); err != nil {
				t.Error(err)
			}
		}()
	}
	wg.Wait()
	if _, tasks, _ := Tasks(root); len(tasks) != 12 || tasks[11].ID != "t12" {
		t.Errorf("%d of 12 tasks kept: %+v", len(tasks), tasks)
	}
}

// A fence left open to the file's end would hide a task put there: refused, the file as it was.
func TestAFenceOpenToTheEndRefusesTheChange(t *testing.T) {
	for _, fence := range []string{"```", "~~~"} {
		root := t.TempDir()
		os.MkdirAll(filepath.Join(root, "design"), 0o755)
		hand := "## To do\n- [ ] Coins ^t1\n\n  - a note\n\n## Doing\n" + fence + "\nexample\n"
		os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte(hand), 0o644)
		_, add := AddTask(root, "Boss", "Doing", nil)
		_, move := MoveTask(root, "t1", "Doing")
		_, hidden := MoveTask(root, "t1", "Done") // its heading would come after the fence
		if add == nil || move == nil || hidden == nil || boardFile(t, root) != hand {
			t.Errorf("%s: %v %v %v\n%s", fence, add, move, hidden, boardFile(t, root))
		}
	}
}

// A removed task's id is never another's: whoever still holds it (another
// tab, an agent) can't move or remove the task added after.
func TestARemovedTasksIDIsntGivenAgain(t *testing.T) {
	root := t.TempDir()
	a, _ := AddTask(root, "A", "", nil)
	if err := RemoveTask(root, a.ID); err != nil {
		t.Fatal(err)
	}
	b, _ := AddTask(root, "B", "", nil)
	if b.ID == a.ID {
		t.Fatalf("B got A's id %s", a.ID)
	}
	if err := RemoveTask(root, a.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := MoveTask(root, a.ID, "Done"); err == nil {
		t.Error("A's id moved something")
	}
	if _, tasks, _ := Tasks(root); len(tasks) != 1 || tasks[0].ID != b.ID {
		t.Errorf("a late removal of A took B: %+v", tasks)
	}
	// lastTask lost by hand: the ids on the board still aren't given again.
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte("## To do\n- [ ] B ^t2\n"), 0o644)
	if c, _ := AddTask(root, "C", "", nil); c.ID != "t3" {
		t.Errorf("after t2: %s", c.ID)
	}
}

// Listing writes nothing: a hand-written task is listed by a draft id from
// its text, which a change finds it by however the board changed between
// (and which finds nothing once its text is changed).
func TestAHandWrittenTaskIsFoundByItsDraftID(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, "design"), 0o755)
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte("## To do\n- [ ] Coins\n- [ ] Boss\n"), 0o644)
	_, tasks, err := Tasks(root)
	if err != nil || len(tasks) != 2 || !strings.HasPrefix(tasks[0].ID, "new-") || tasks[0].ID == tasks[1].ID {
		t.Fatalf("listed: %+v %v", tasks, err)
	}
	if got := boardFile(t, root); got != "## To do\n- [ ] Coins\n- [ ] Boss\n" {
		t.Errorf("listing wrote:\n%s", got)
	}
	boss := tasks[1].ID
	text := strings.Replace(boardFile(t, root), "## To do\n", "## To do\n- [ ] Pits\n", 1) // written above by hand
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte(text), 0o644)
	moved, err := MoveTask(root, boss, "Done")
	if err != nil || moved.Text != "Boss" || !strings.HasPrefix(moved.ID, "t") {
		t.Fatalf("moving %s moved %+v (%v)", boss, moved, err)
	}
	if _, tasks, _ = Tasks(root); len(tasks) != 3 || tasks[2].ID != moved.ID || tasks[2].Column != "Done" {
		t.Errorf("after: %+v", tasks)
	}
	if _, err := MoveTask(root, boss, "Doing"); err == nil {
		t.Error("a draft id outlived its task getting a real one")
	}
}

// Hand-written tasks alike (one renamed to the other's text since listing)
// share a draft id, which names neither: a change by it is refused.
func TestADraftIDOfTasksAlikeIsRefused(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, "design"), 0o755)
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte("## To do\n- [ ] Coins\n- [ ] Boss\n"), 0o644)
	_, tasks, _ := Tasks(root)
	boss := tasks[1].ID
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte("## To do\n- [ ] Boss\n- [ ] Boss\n"), 0o644) // Coins renamed by hand
	if _, err := MoveTask(root, boss, "Done"); err == nil || !strings.Contains(err.Error(), "more than one task") {
		t.Errorf("moved by an id of two tasks: %v", err)
	}
	if err := RemoveTask(root, boss); err == nil {
		t.Error("removed by an id of two tasks")
	}
	if got := boardFile(t, root); got != "## To do\n- [ ] Boss\n- [ ] Boss\n" {
		t.Errorf("the board changed:\n%s", got)
	}
}

// lastTask is read as YAML writes it, and kept once.
func TestLastTaskIsReadAsYAML(t *testing.T) {
	for _, front := range []string{"lastTask: 't7'", "lastTask: t7 # the highest", `lastTask: "t7"`} {
		root := t.TempDir()
		os.MkdirAll(filepath.Join(root, "design"), 0o755)
		os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte("---\ntitle: Tasks\n"+front+"\n---\n\n## To do\n"), 0o644)
		if a, _ := AddTask(root, "A", "", nil); a.ID != "t8" {
			t.Errorf("%s: added %s", front, a.ID)
		}
		if got := boardFile(t, root); strings.Count(got, "lastTask") != 1 || !strings.Contains(got, "lastTask: t8") {
			t.Errorf("%s: written\n%s", front, got)
		}
	}
}

// A removed task's copy (its ^id the same, listed by a draft) doesn't become it.
func TestRemovingATaskLeavesItsCopyItsOwn(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, "design"), 0o755)
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte("## To do\n- [ ] Original ^t1\n- [ ] Copy ^t1\n"), 0o644)
	if err := RemoveTask(root, "t1"); err != nil {
		t.Fatal(err)
	}
	if err := RemoveTask(root, "t1"); err != nil {
		t.Fatal(err)
	}
	if _, tasks, _ := Tasks(root); len(tasks) != 1 || tasks[0].Text != "Copy" || tasks[0].ID == "t1" {
		t.Errorf("after removing t1 twice: %+v", tasks)
	}
}

// lastTask is raised as the frontmatter is written: indented alike; one it
// can't be (an anchor others use) refuses the change rather than lose count.
func TestLastTaskKeepsTheFrontmattersShape(t *testing.T) {
	for front, want := range map[string]string{
		"  title: Tasks\n  lastTask: t7":                  "  title: Tasks\n  lastTask: t8\n",
		"  title: Tasks":                                  "---\n  lastTask: t8\n  title: Tasks\n",
		"lastTask: |-\n  t7\ntitle: Tasks":                "\nlastTask: t8\ntitle: Tasks\n",
		"lastTask: t7\n# kept by hand\ntitle: Tasks":      "\nlastTask: t8\n# kept by hand\ntitle: Tasks\n",
		"lastTask: t7 # do not reset\ntitle: Tasks":       "\nlastTask: t8 # do not reset\ntitle: Tasks\n",
		"lastTask:\n  # do not reset\n  t7\ntitle: Tasks": "\nlastTask: t8\n  # do not reset\ntitle: Tasks\n",
	} {
		root := t.TempDir()
		os.MkdirAll(filepath.Join(root, "design"), 0o755)
		os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte("---\n"+front+"\n---\n\n## To do\n- [ ] Old ^t7\n"), 0o644)
		if _, err := AddTask(root, "A", "", nil); err != nil {
			t.Fatal(err)
		}
		got := boardFile(t, root)
		if _, _, err := frontmatter(got); err != nil || !strings.Contains(got, want) {
			t.Errorf("%q: %v\n%s", front, err, got)
		}
	}
}

func TestALastTaskThatCantBeRaisedRefusesTheChange(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, "design"), 0o755)
	board := "---\nlastTask: &n t7\nnext: *n\n---\n\n## To do\n"
	os.WriteFile(filepath.Join(root, "design", "Tasks.md"), []byte(board), 0o644)
	if a, err := AddTask(root, "A", "", nil); err == nil {
		t.Errorf("added %s with lastTask kept at t7", a.ID)
	}
	if got := boardFile(t, root); got != board {
		t.Errorf("written:\n%s", got)
	}
}
