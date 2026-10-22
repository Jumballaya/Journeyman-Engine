package memory

import (
	"crypto/sha256"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strconv"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
)

// The game's task board: design/Tasks.md, a column per "## " heading and a
// task per "- [ ] text ^t3" line (^t3: its id, an Obsidian block id). A task
// links what it's about: [[Note]], `game/file.json`, jm://play/<id>/m2 (a
// marker), jm://entity?scene=…&name=… (an entity). Changes patch the file's
// lines: whatever else the person wrote there stays. Its lastTask property
// is the highest id given: a removed task's id is never another's. A task
// written without one is listed by a draft id (new-…, from its text) that a
// change finds it by; a change gives each draft a real id.

// Columns are a new board's, in order; a task moved to Done is ticked.
var Columns = []string{"To do", "Doing", "Done"}

const tasksPath = Dir + "/Tasks.md"

// Task is one task. Links are as written: "[[Note]]", a file, or a jm:// link.
// Notes are the indented lines under it (blank lines between them allowed).
type Task struct {
	ID     string   `json:"id"`
	Text   string   `json:"text"`
	Column string   `json:"column"`
	Links  []string `json:"links"`
	Notes  []string `json:"notes,omitempty"`
}

var (
	taskLine = regexp.MustCompile(`^- \[([ xX])\] (.*?)(?:\s+\^(t\d+))?\s*$`)
	taskLink = regexp.MustCompile("(\\[\\[[^\\]]+\\]\\])|`([^`]+)`|(jm://[^\\s`)\\]>]+)")
)

// board is Tasks.md as lines, with where each column and task is.
type board struct {
	lines   []string
	columns []string
	tasks   []Task
	at, end []int // each task's line, and the line after its block (it and its notes)
	last    int   // the highest id given, kept over scans
	kept    int   // the lastTask the file has (save raises it to last)
}

// readBoard reads Tasks.md; a new board's page when there's none.
func readBoard(root string) (*board, error) {
	b := &board{}
	data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(tasksPath)))
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}
	if err != nil {
		b.lines = strings.Split(strings.TrimSuffix(page("Tasks", "tasks", "", "What's being done to the game, by the person and their agent (`jm tasks`). A task links what it's about.\n",
			"\n## "+strings.Join(Columns, "\n\n## ")+"\n"), "\n"), "\n")
	} else {
		b.lines = strings.Split(strings.TrimSuffix(strings.ReplaceAll(string(data), "\r\n", "\n"), "\n"), "\n")
	}
	b.scan()
	return b, nil
}

func (b *board) scan() {
	b.columns, b.tasks, b.at, b.end = nil, nil, nil, nil
	column, code, open := "", codeBlocks(b.lines), false // open: the last task's block goes on
	if front, _, err := frontmatter(b.text()); err == nil {
		b.kept = lastTaskOf(front)
	}
	b.last = max(b.last, b.kept)
	seen := map[string]bool{}
	for i, line := range b.lines {
		indented := line == "" || line[0] == ' ' || line[0] == '\t'
		if n := len(b.tasks); open && (indented || code[i] > 0 && code[i] == code[i-1]) { // a fence it opened goes on to its end
			if strings.TrimSpace(line) != "" {
				b.tasks[n-1].Notes, b.end[n-1] = append(b.tasks[n-1].Notes, line), i+1
			}
			continue
		}
		open = false
		if code[i] > 0 {
			continue
		}
		if h, ok := strings.CutPrefix(line, "## "); ok {
			column = strings.TrimSpace(h)
			b.columns = append(b.columns, column)
		} else if m := taskLine.FindStringSubmatch(line); m != nil && column != "" {
			t := Task{ID: m[3], Text: m[2], Column: column, Links: links(m[2])}
			if v, ok := idNumber(t.ID); ok && !seen[t.ID] {
				seen[t.ID], b.last = true, max(b.last, v)
			} else { // none, or a copied task's: a draft, below
				t.ID = ""
			}
			if m[1] != " " { // ticked by hand (Obsidian): done, whatever column it's in
				t.Column = "Done"
			}
			b.tasks, b.at, b.end, open = append(b.tasks, t), append(b.at, i), append(b.end, i+1), true
		}
	}
	// A draft id is its text's as of the board's highest id: two alike share
	// one (find refuses it), and a change giving ids retires every draft listed.
	for i, t := range b.tasks {
		if t.ID == "" {
			b.tasks[i].ID = "new-" + fmt.Sprintf("%x", sha256.Sum256([]byte(strconv.Itoa(b.last)+"\x00"+t.Text)))[:16]
		}
	}
	for _, c := range Columns {
		if !slices.Contains(b.columns, c) {
			b.columns = append(b.columns, c)
		}
	}
}

// idNumber is a real id's number (t3: 3).
func idNumber(id string) (int, bool) {
	n, ok := strings.CutPrefix(id, "t")
	v, err := strconv.Atoi(n)
	return v, ok && err == nil && v > 0
}

// ided gives each task listed by a draft id a real one, in its line.
func (b *board) ided() {
	for i, t := range b.tasks {
		if _, real := idNumber(t.ID); !real {
			b.tasks[i].ID = b.newID()
			b.lines[b.at[i]] = taskText(b.tasks[i])
		}
	}
}

// newID is an id no task has had.
func (b *board) newID() string {
	b.last++
	return "t" + strconv.Itoa(b.last)
}

func taskText(t Task) string {
	tick := " "
	if t.Column == "Done" {
		tick = "x"
	}
	return fmt.Sprintf("- [%s] %s ^%s", tick, t.Text, t.ID)
}

// put inserts lines at the end of a column's section, making the section at
// the file's end; refused where they'd land in a fence left open.
func (b *board) put(column string, lines ...string) error {
	start, code := -1, fenced(b.lines)
	heading := func(i int) bool { return !code[i] && strings.HasPrefix(b.lines[i], "## ") }
	for i, l := range b.lines {
		if heading(i) && strings.TrimSpace(l[3:]) == column {
			start = i
		}
	}
	if start < 0 {
		b.lines = append(b.lines, "", "## "+column)
		start, code = len(b.lines)-1, fenced(b.lines)
	}
	end := start + 1 // after the section's last non-blank line
	for i := start + 1; i < len(b.lines) && !heading(i); i++ {
		if strings.TrimSpace(b.lines[i]) != "" {
			end = i + 1
		}
	}
	b.lines = slices.Insert(b.lines, end, lines...)
	if codeBlocks(b.lines)[end] > 0 {
		return fmt.Errorf("a code fence (``` or ~~~) in %s is left open to its end: close it, then try again", tasksPath)
	}
	b.scan()
	return nil
}

// find is the task id names (-1: none); an error when it names more than one
// (hand-written tasks alike share a draft id: which one it meant isn't known).
func (b *board) find(id string) (int, error) {
	at := -1
	for i, t := range b.tasks {
		if t.ID != id {
			continue
		}
		if at >= 0 {
			return -1, fmt.Errorf("more than one task reads %q: give one an id (^t…) or new text in %s", t.Text, tasksPath)
		}
		at = i
	}
	return at, nil
}

func (b *board) text() string { return strings.Join(b.lines, "\n") + "\n" }

// save writes the board, its lastTask raised to the highest id given; not at
// all when that can't be kept (an id given now could be given again).
func (b *board) save(root string) error {
	text := b.text()
	if b.last > b.kept {
		var err error
		if text, err = withLastTask(text, b.last); err != nil {
			return err
		}
	}
	return write(root, tasksPath, text)
}

// withLastTask is text with lastTask: t<last> in its frontmatter (made for it
// if it has none), indented as the frontmatter is; an error when YAML wouldn't
// read it back so (its frontmatter unreadable, an anchor others use).
func withLastTask(text string, last int) (string, error) {
	refused := fmt.Errorf("%s's frontmatter can't take lastTask: t%d (the highest task id given): make lastTask a plain value, then try again", tasksPath, last)
	front, _, err := frontmatter(text)
	if err != nil {
		return "", fmt.Errorf("%w: %v", refused, err)
	}
	mark := "lastTask: t" + strconv.Itoa(last)
	if len(front) > 0 {
		mark = front[0].raw[:len(front[0].raw)-len(strings.TrimLeft(front[0].raw, " "))] + mark
	}
	out := "---\n" + mark + "\n---\n\n" + text
	if at := slices.IndexFunc(front, func(p property) bool { return p.key == "lastTask" }); at >= 0 {
		lines := strings.Split(front[at].raw, "\n")
		value := len(lines) // its value's lines (a block's too); the comments and blank lines after it stay
		for value > 1 && (strings.TrimSpace(lines[value-1]) == "" || strings.HasPrefix(strings.TrimSpace(lines[value-1]), "#")) {
			value--
		}
		for _, c := range []string{front[at].keyNode.LineComment, front[at].node.LineComment} { // its line's comment too
			if c != "" {
				mark += " " + c
			}
		}
		kept := []string{mark}
		for _, line := range lines[1:value] { // and those among its value's lines
			if strings.HasPrefix(strings.TrimSpace(line), "#") {
				kept = append(kept, line)
			}
		}
		out = strings.Replace(text, "\n"+front[at].raw+"\n", "\n"+strings.Join(append(kept, lines[value:]...), "\n")+"\n", 1)
	} else if strings.HasPrefix(text, "---\n") && strings.Contains(text[3:], "\n---\n") {
		out = "---\n" + mark + "\n" + text[4:]
	}
	if after, _, err := frontmatter(out); err != nil || lastTaskOf(after) != last {
		return "", refused
	}
	return out, nil
}

// lastTaskOf is the frontmatter's lastTask (0: none, or not an id).
func lastTaskOf(front []property) int {
	for _, p := range front {
		if v, ok := idNumber(p.text()); ok && p.key == "lastTask" {
			return v
		}
	}
	return 0
}

// Tasks is the board: its columns (a new board's, then any the person added)
// and its tasks in file order; none without a board.
func Tasks(root string) ([]string, []Task, error) {
	b, err := readBoard(root)
	if err != nil {
		return nil, nil, err
	}
	if b.tasks == nil {
		b.tasks = []Task{}
	}
	return b.columns, b.tasks, nil
}

// change runs a change to the board while holding the game, then saves it if asked.
func change(root string, f func(b *board) (save bool, err error)) error {
	release, err := gamelock.Hold()
	if err != nil {
		return err
	}
	defer release()
	b, err := readBoard(root)
	if err != nil {
		return err
	}
	if save, err := f(b); err != nil || !save {
		return err
	}
	return b.save(root)
}

func links(text string) []string {
	out := []string{}
	for _, m := range taskLink.FindAllStringSubmatch(text, -1) {
		out = append(out, strings.TrimSpace(m[1]+m[2]+m[3]))
	}
	return out
}

// AddTask puts a task at the end of a column ("": To do), with links (notes'
// titles, game files, jm:// links) after its text.
func AddTask(root, text, column string, with []string) (t Task, err error) {
	text = oneLine(text)
	if text == "" {
		return Task{}, fmt.Errorf("say what the task is")
	}
	for _, l := range with {
		switch l = strings.TrimSpace(l); {
		case l == "":
		case strings.HasPrefix(l, "[[") || strings.HasPrefix(l, "jm://"):
			text += " · " + l
		case strings.Contains(l, "/") || strings.Contains(l, "."):
			text += " · `" + strings.Trim(l, "`") + "`"
		default:
			text += " · [[" + l + "]]"
		}
	}
	return t, change(root, func(b *board) (bool, error) {
		column, err := columnOf(b, column)
		if err != nil {
			return false, err
		}
		b.ided()
		t = Task{ID: b.newID(), Text: text, Column: column, Links: links(text)}
		return true, b.put(column, taskText(t))
	})
}

// MoveTask moves a task (and its notes) to the end of a column; to Done ticks it.
func MoveTask(root, id, column string) (t Task, err error) {
	return t, change(root, func(b *board) (bool, error) {
		column, err := columnOf(b, column)
		if err != nil {
			return false, err
		}
		i, err := b.find(id) // first: a draft id is the board's as listed
		if err != nil {
			return false, err
		}
		if i < 0 {
			return false, fmt.Errorf("no task %s (the board lists them)", id)
		}
		b.ided()
		t = b.tasks[i]
		t.Column = column
		block := append([]string{taskText(t)}, b.lines[b.at[i]+1:b.end[i]]...)
		b.lines = slices.Delete(b.lines, b.at[i], b.end[i])
		b.scan()
		return true, b.put(column, block...)
	})
}

// RemoveTask takes a task (and its notes) off the board; one that isn't there is already off.
func RemoveTask(root, id string) error {
	return change(root, func(b *board) (bool, error) {
		i, err := b.find(id)
		if i < 0 {
			return false, err
		}
		b.ided() // first: a copy of it (a draft, its ^id the same) isn't left to become it
		b.lines = slices.Delete(b.lines, b.at[i], b.end[i])
		return true, nil
	})
}

func columnOf(b *board, name string) (string, error) {
	if name == "" {
		return Columns[0], nil
	}
	for _, c := range b.columns {
		if strings.EqualFold(c, strings.TrimSpace(name)) {
			return c, nil
		}
	}
	return "", fmt.Errorf("no column %q: %s", name, strings.Join(b.columns, ", "))
}
