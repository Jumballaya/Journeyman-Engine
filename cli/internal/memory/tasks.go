package memory

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strconv"
	"strings"
)

// The game's task board: design/Tasks.md, a column per "## " heading and a
// task per "- [ ] text ^t3" line (^t3: its id, an Obsidian block id). A task
// links what it's about: [[Note]], `game/file.json`, jm://play/<id>/m2 (a
// marker), jm://entity?scene=…&name=… (an entity). Changes patch the file's
// lines: whatever else the person wrote there stays.

// Columns are a new board's, in order; a task moved to Done is ticked.
var Columns = []string{"To do", "Doing", "Done"}

const tasksPath = Dir + "/Tasks.md"

// Task is one task. Links are as written: "[[Note]]", a file, or a jm:// link.
// Notes are the indented lines under it.
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
	at      []int // each task's line
}

// span is how many lines task i takes: its line and its notes.
func (b *board) span(i int) int { return 1 + len(b.tasks[i].Notes) }

func readBoard(root string) *board {
	b := &board{}
	data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(tasksPath)))
	if err != nil {
		b.lines = strings.Split(strings.TrimSuffix(page("Tasks", "tasks", "", "What's being done to the game, by the person and their agent (`jm tasks`). A task links what it's about.\n",
			"\n## "+strings.Join(Columns, "\n\n## ")+"\n"), "\n"), "\n")
	} else {
		b.lines = strings.Split(strings.TrimSuffix(strings.ReplaceAll(string(data), "\r\n", "\n"), "\n"), "\n")
	}
	b.scan()
	return b
}

func (b *board) scan() {
	b.columns, b.tasks, b.at = nil, nil, nil
	column, fenced := "", false
	for i, line := range b.lines {
		if strings.HasPrefix(strings.TrimSpace(line), "```") {
			fenced = !fenced
		}
		if fenced {
			continue
		}
		if h, ok := strings.CutPrefix(line, "## "); ok {
			column = strings.TrimSpace(h)
			b.columns = append(b.columns, column)
		} else if m := taskLine.FindStringSubmatch(line); m != nil && column != "" {
			t := Task{ID: m[3], Text: m[2], Column: column, Links: links(m[2])}
			if m[1] != " " { // ticked by hand (Obsidian): done, whatever column it's in
				t.Column = "Done"
			}
			b.tasks, b.at = append(b.tasks, t), append(b.at, i)
		} else if n := len(b.tasks); n > 0 && line != "" && (line[0] == ' ' || line[0] == '\t') && b.at[n-1]+b.span(n-1) == i {
			b.tasks[n-1].Notes = append(b.tasks[n-1].Notes, line)
		}
	}
	for _, c := range Columns {
		if !slices.Contains(b.columns, c) {
			b.columns = append(b.columns, c)
		}
	}
}

// ided gives every task an id, a copied task's id a new one: true when it changed the lines.
func (b *board) ided() bool {
	next, seen, changed := b.nextID(), map[string]bool{}, false
	for i, t := range b.tasks {
		if t.ID == "" || seen[t.ID] {
			b.tasks[i].ID = "t" + strconv.Itoa(next)
			next++
			b.lines[b.at[i]] = taskText(b.tasks[i])
			changed = true
		}
		seen[b.tasks[i].ID] = true
	}
	return changed
}

// nextID is the number after the highest task id.
func (b *board) nextID() int {
	next := 1
	for _, t := range b.tasks {
		if v, err := strconv.Atoi(strings.TrimPrefix(t.ID, "t")); err == nil && v >= next {
			next = v + 1
		}
	}
	return next
}

func taskText(t Task) string {
	tick := " "
	if t.Column == "Done" {
		tick = "x"
	}
	return fmt.Sprintf("- [%s] %s ^%s", tick, t.Text, t.ID)
}

// put inserts lines at the end of a column's section, making the section at the file's end.
func (b *board) put(column string, lines ...string) {
	start := -1
	for i, l := range b.lines {
		if strings.TrimSpace(strings.TrimPrefix(l, "## ")) == column && strings.HasPrefix(l, "## ") {
			start = i
		}
	}
	if start < 0 {
		b.lines = append(b.lines, "", "## "+column)
		start = len(b.lines) - 1
	}
	end := start + 1 // after the section's last non-blank line
	for i := start + 1; i < len(b.lines) && !strings.HasPrefix(b.lines[i], "## "); i++ {
		if strings.TrimSpace(b.lines[i]) != "" {
			end = i + 1
		}
	}
	b.lines = slices.Insert(b.lines, end, lines...)
	b.scan()
}

func (b *board) find(id string) int {
	return slices.IndexFunc(b.tasks, func(t Task) bool { return t.ID == id })
}

func (b *board) save(root string) error {
	return write(root, tasksPath, strings.Join(b.lines, "\n")+"\n")
}

// Tasks is the board: its columns (a new board's, then any the person added)
// and its tasks in file order; none without a board.
func Tasks(root string) ([]string, []Task) {
	b := readBoard(root)
	b.ided() // ids for display; written with the next change
	if b.tasks == nil {
		b.tasks = []Task{}
	}
	return b.columns, b.tasks
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
func AddTask(root, text, column string, with []string) (Task, error) {
	text = oneLine(text)
	if text == "" {
		return Task{}, fmt.Errorf("say what the task is")
	}
	b := readBoard(root)
	column, err := columnOf(b, column)
	if err != nil {
		return Task{}, err
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
	b.ided()
	t := Task{ID: "t" + strconv.Itoa(b.nextID()), Text: text, Column: column, Links: links(text)}
	b.put(column, taskText(t))
	return t, b.save(root)
}

// MoveTask moves a task (and its notes) to the end of a column; to Done ticks it.
func MoveTask(root, id, column string) (Task, error) {
	b := readBoard(root)
	column, err := columnOf(b, column)
	if err != nil {
		return Task{}, err
	}
	b.ided()
	i := b.find(id)
	if i < 0 {
		return Task{}, fmt.Errorf("no task %s (the board lists them)", id)
	}
	t := b.tasks[i]
	t.Column = column
	block := append([]string{taskText(t)}, t.Notes...)
	b.lines = slices.Delete(b.lines, b.at[i], b.at[i]+b.span(i))
	b.scan()
	b.put(column, block...)
	return t, b.save(root)
}

// RemoveTask takes a task (and its notes) off the board; one that isn't there is already off.
func RemoveTask(root, id string) error {
	b := readBoard(root)
	changed := b.ided()
	if i := b.find(id); i >= 0 {
		b.lines = slices.Delete(b.lines, b.at[i], b.at[i]+b.span(i))
		changed = true
	}
	if !changed {
		return nil
	}
	return b.save(root)
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
