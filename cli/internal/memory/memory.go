// Package memory is a game's design notes and what was decided: design/, a
// folder of Markdown notes that Obsidian opens as a vault and Basic Memory
// can index (a note per thing: frontmatter, observations "- [category] text",
// relations "- kind [[Note]]").
package memory

import (
	"errors"
	"fmt"
	"io/fs"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
)

// Dir is the notes' folder in a game.
const Dir = "design"

// Note is one note. Path is from the game's folder, with slashes; Links are
// the [[targets]] it names, each once, in order.
type Note struct {
	Title        string        `json:"title"`
	Path         string        `json:"path"`
	Type         string        `json:"type,omitempty"`
	Observations []Observation `json:"observations"`
	Links        []string      `json:"links"`
	Text         string        `json:"-"`
}

type Observation struct {
	Category string `json:"category"`
	Text     string `json:"text"`
}

// Scaffold writes the notes a game starts with, keeping any that exist:
// the paths it made.
func Scaffold(root string) ([]string, error) {
	release, err := gamelock.Hold()
	if err != nil {
		return nil, err
	}
	defer release()
	return scaffoldHeld(root)
}

// scaffoldHeld is Scaffold for a caller holding the game.
func scaffoldHeld(root string) ([]string, error) {
	var made []string
	for _, f := range scaffold {
		if _, err := os.Lstat(filepath.Join(root, Dir, filepath.FromSlash(f.path))); err == nil {
			continue
		}
		if err := write(root, Dir+"/"+f.path, f.text); err != nil {
			return made, err
		}
		made = append(made, Dir+"/"+f.path)
	}
	return made, nil
}

// Notes are the game's notes, sorted by path; none without design/.
func Notes(root string) []Note {
	var out []Note
	dir := filepath.Join(root, Dir) // design/ may link to a folder in the game; one leading out isn't read
	realRoot, err := filepath.EvalSymlinks(root)
	if real, err2 := filepath.EvalSymlinks(dir); err == nil && err2 == nil {
		if within, err := filepath.Rel(realRoot, real); err == nil && filepath.IsLocal(within) {
			dir = real
		}
	}
	_ = filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() && strings.HasPrefix(d.Name(), ".") {
			return filepath.SkipDir // .obsidian
		}
		if !d.Type().IsRegular() || !strings.HasSuffix(path, ".md") {
			return nil
		}
		if data, err := os.ReadFile(path); err == nil {
			rel, _ := filepath.Rel(dir, path)
			out = append(out, parse(Dir+"/"+filepath.ToSlash(rel), string(data)))
		}
		return nil
	})
	sort.Slice(out, func(i, j int) bool { return out[i].Path < out[j].Path })
	return out
}

// Find is the note name means: its title or file name (any case), or path.
func Find(root, name string) (Note, bool) {
	key := strings.ToLower(strings.TrimSuffix(strings.TrimSpace(name), ".md"))
	for _, n := range Notes(root) {
		if strings.ToLower(n.Title) == key || strings.ToLower(strings.TrimSuffix(n.Path, ".md")) == key ||
			strings.ToLower(strings.TrimSuffix(filepath.Base(n.Path), ".md")) == key {
			return n, true
		}
	}
	return Note{}, false
}

// Backlinks are the titles of the notes that link to n.
func Backlinks(root string, n Note) []string {
	out := []string{}
	for _, other := range Notes(root) {
		for _, l := range other.Links {
			if other.Path != n.Path && linksTo(l, n) {
				out = append(out, other.Title)
				break
			}
		}
	}
	return out
}

// linksTo: a [[link]] names a note by title or file name, any case (as Obsidian resolves it).
func linksTo(link string, n Note) bool {
	l := strings.ToLower(strings.TrimSuffix(link, ".md"))
	return l == strings.ToLower(n.Title) || l == strings.ToLower(strings.TrimSuffix(filepath.Base(n.Path), ".md")) ||
		l == strings.ToLower(strings.TrimSuffix(strings.TrimPrefix(n.Path, Dir+"/"), ".md"))
}

// Hit is a note whose title (Line 0) or one of whose lines holds the text searched for.
type Hit struct {
	Title string `json:"title"`
	Path  string `json:"path"`
	Line  int    `json:"line,omitempty"`
	Text  string `json:"text,omitempty"`
}

// Search finds text, in any case, in the notes' titles and lines.
func Search(root, text string) []Hit {
	out := []Hit{}
	needle := strings.ToLower(strings.TrimSpace(text))
	if needle == "" {
		return out
	}
	for _, n := range Notes(root) {
		if strings.Contains(strings.ToLower(n.Title), needle) {
			out = append(out, Hit{Title: n.Title, Path: n.Path})
		}
		for i, line := range strings.Split(n.Text, "\n") {
			if strings.Contains(strings.ToLower(line), needle) {
				out = append(out, Hit{Title: n.Title, Path: n.Path, Line: i + 1, Text: strings.TrimSpace(line)})
			}
		}
	}
	return out
}

// Observe adds "- [category] text" to a note's observations (category
// "note" when empty), making the scaffold first and the note if there's none
// of that name.
func Observe(root, note, category, text string) (Note, error) {
	text = oneLine(text)
	if text == "" {
		return Note{}, errors.New("say what to note")
	}
	if category = oneLine(strings.Trim(category, "[] ")); category == "" {
		category = "note"
	}
	release, err := gamelock.Hold()
	if err != nil {
		return Note{}, err
	}
	defer release()
	if _, err := scaffoldHeld(root); err != nil {
		return Note{}, err
	}
	title := fileTitle(note)
	n, ok := Find(root, note)
	if !ok {
		n, ok = Find(root, title)
	}
	if !ok {
		if title == "" {
			return Note{}, fmt.Errorf("%q can't be a note's name", note)
		}
		n = parse(Dir+"/"+title+".md", page(title, "note", "", "", "\n## Relations\n- part_of [[Design]]\n"))
	}
	text = addToSection(n.Text, "Observations", fmt.Sprintf("- [%s] %s", category, text))
	if err := write(root, n.Path, text); err != nil {
		return Note{}, err
	}
	return parse(n.Path, text), nil
}

// Decide records a decision as its own note in design/decisions (why: the
// reasons, [[links]] welcome), listed in the Decisions note.
func Decide(root, title, why string) (Note, error) {
	title, why = fileTitle(title), strings.TrimSpace(why)
	if title == "" || why == "" {
		return Note{}, errors.New("a decision needs a title and why")
	}
	release, err := gamelock.Hold()
	if err != nil {
		return Note{}, err
	}
	defer release()
	if _, err := scaffoldHeld(root); err != nil {
		return Note{}, err
	}
	path := Dir + "/decisions/" + title + ".md"
	index, indexed := Find(root, "Decisions")
	if n, ok := Find(root, title); ok {
		if n.Path != path || !indexed || slices.ContainsFunc(index.Links, func(l string) bool { return linksTo(l, n) }) {
			return Note{}, fmt.Errorf("%s already exists: add to it with a note instead", n.Path)
		}
		return n, addToIndex(root, index, title) // a decision whose index update failed: finish it
	}
	text := page(title, "decision", "date: "+time.Now().Format("2006-01-02")+"\n", why+"\n",
		fmt.Sprintf("\n## Observations\n- [decision] %s\n- [why] %s\n\n## Relations\n- part_of [[Decisions]]\n", title, oneLine(why)))
	if err := write(root, path, text); err != nil {
		return Note{}, err
	}
	if !indexed {
		return parse(path, text), nil
	}
	return parse(path, text), addToIndex(root, index, title)
}

func addToIndex(root string, index Note, title string) error {
	if err := write(root, index.Path, addToSection(index.Text, "Relations", "- includes [["+title+"]]")); err != nil {
		return fmt.Errorf("the decision is saved, but %s couldn't list it (%v): decide it again to finish", index.Path, err)
	}
	return nil
}

// ObsidianURL opens a note in Obsidian (in the vault holding it, once the person opened design/ as one).
func ObsidianURL(root string, n Note) string {
	abs, _ := filepath.Abs(filepath.Join(root, filepath.FromSlash(n.Path)))
	return "obsidian://open?path=" + url.QueryEscape(abs)
}

var (
	observation = regexp.MustCompile(`^\s*- \[([^\]]+)\]\s+(.+)$`)
	wikilink    = regexp.MustCompile(`\[\[([^\]|#]+)(?:[#|][^\]]*)?\]\]`)
)

func parse(path, text string) Note {
	text = strings.ReplaceAll(text, "\r\n", "\n") // Windows' Obsidian
	n := Note{Path: path, Text: text, Observations: []Observation{}, Links: []string{}}
	body := text
	if rest, ok := strings.CutPrefix(text, "---\n"); ok {
		if front, after, ok := strings.Cut(rest, "\n---\n"); ok {
			body = after
			for _, line := range strings.Split(front, "\n") {
				key, value, _ := strings.Cut(line, ":")
				switch strings.TrimSpace(key) {
				case "title":
					n.Title = yamlScalar(value)
				case "type":
					n.Type = strings.TrimSpace(value)
				}
			}
		}
	}
	if n.Title == "" {
		n.Title = strings.TrimSuffix(filepath.Base(path), ".md")
	}
	seen := map[string]bool{}
	lines := strings.Split(body, "\n")
	for i, line := range prose(lines) {
		// Its marker found outside code; its text as written, after the marker (prose keeps every column).
		if m := observation.FindStringSubmatchIndex(line); m != nil {
			n.Observations = append(n.Observations, Observation{lines[i][m[2]:m[3]], strings.TrimSpace(lines[i][m[3]+1:])})
		}
		for _, m := range wikilink.FindAllStringSubmatch(line, -1) {
			if target := strings.TrimSpace(m[1]); !seen[target] {
				seen[target] = true
				n.Links = append(n.Links, target)
			}
		}
	}
	return n
}

// addToSection adds line at the end of text's "## heading" section, making
// the section (before Relations, or at the end) when there's none.
func addToSection(text, heading, line string) string {
	lines := strings.Split(strings.TrimRight(text, "\n"), "\n")
	inCode, heads := fenced(lines), make([]bool, len(lines))
	for i, l := range lines {
		heads[i] = !inCode[i] && strings.HasPrefix(l, "## ") // a heading in an example isn't one
	}
	start := -1
	for i, l := range lines {
		if heads[i] && strings.TrimSpace(l) == "## "+heading {
			start = i
			break
		}
	}
	if start < 0 {
		at := len(lines)
		for i, l := range lines {
			if heading != "Relations" && heads[i] && strings.TrimSpace(l) == "## Relations" {
				at = i
				break
			}
		}
		section := []string{"## " + heading, line, ""}
		if at == len(lines) {
			section = append([]string{""}, section[:2]...)
		}
		lines = append(lines[:at], append(section, lines[at:]...)...)
		return strings.Join(lines, "\n") + "\n"
	}
	end := start + 1 // after the section's last non-blank line
	for i := start + 1; i < len(lines) && !heads[i]; i++ {
		if strings.TrimSpace(lines[i]) != "" {
			end = i + 1
		}
	}
	lines = append(lines[:end], append([]string{line}, lines[end:]...)...)
	return strings.Join(lines, "\n") + "\n"
}

// fenced marks the lines of fenced code blocks (``` or ~~~ indented at most
// 3 spaces, closed by the same character, at least as many), their fences too.
func fenced(lines []string) []bool {
	out := make([]bool, len(lines))
	fence := ""                  // the open block's fence
	blank, inList := true, false // the line before is blank (or none); the last text was a list's
	for i, l := range lines {
		if fence == "" && isIndentedCode(l) && (blank || i > 0 && out[i-1]) && !inList {
			out[i] = true
			continue
		}
		if strings.TrimSpace(l) == "" {
			out[i], blank = fence != "", true
			continue
		}
		if listItem.MatchString(l) {
			inList = true
		} else if blank && !isIndentedCode(l) {
			inList = false // text after a blank line, not indented under the list
		}
		blank = false
		t := strings.TrimLeft(l, " ")
		mark := ""
		for _, c := range []string{"`", "~"} {
			if run := len(t) - len(strings.TrimLeft(t, c)); run >= 3 && len(l)-len(t) <= 3 {
				mark = t[:run]
			}
		}
		if mark != "" && mark[0] == '`' && strings.Contains(t[len(mark):], "`") {
			mark = "" // a code span on one line, not a fence
		}
		switch {
		case fence == "" && mark != "":
			fence = mark
		case fence != "" && mark != "" && mark[0] == fence[0] && len(mark) >= len(fence) && strings.TrimSpace(t[len(mark):]) == "":
			out[i], fence = true, ""
			continue
		}
		out[i] = fence != ""
	}
	return out
}

// isIndentedCode: indented 4 spaces or a tab (an indented code block's line, after a blank one).
func isIndentedCode(l string) bool {
	return strings.TrimSpace(l) != "" && (strings.HasPrefix(l, "    ") || strings.HasPrefix(l, "\t"))
}

// prose is lines with their code taken out (made blank): fenced blocks, and
// `code` spans (a run of backticks to the next as long, within a paragraph).
func prose(lines []string) []string {
	inCode := fenced(lines)
	out := make([]string, len(lines))
	for start := 0; start < len(lines); {
		if inCode[start] || strings.TrimSpace(lines[start]) == "" {
			start++ // left blank
			continue
		}
		end := start + 1 // a paragraph: lines up to a blank, fenced, list item or heading one
		for end < len(lines) && !inCode[end] && strings.TrimSpace(lines[end]) != "" && !blockStart.MatchString(lines[end]) {
			end++
		}
		copy(out[start:end], strings.Split(blankSpans(strings.Join(lines[start:end], "\n")), "\n"))
		start = end
	}
	return out
}

// listItem starts a list item; blockStart, a block of its own (a list item or a heading).
var (
	listItem   = regexp.MustCompile(`^ {0,3}([-*+]|\d+[.)])( |$)`)
	blockStart = regexp.MustCompile(listItem.String() + `|^ {0,3}#`)
)

// blankSpans is text with each code span's characters made spaces, its line
// breaks kept. A \` is a backtick, not a span's start.
func blankSpans(text string) string {
	b := []byte(text)
	run := func(i int) int {
		n := 0
		for i+n < len(b) && b[i+n] == '`' {
			n++
		}
		return n
	}
	for i := 0; i < len(b); {
		n := run(i)
		if n == 0 || i > 0 && b[i-1] == '\\' {
			i += max(n, 1)
			continue
		}
		end := -1
		for j := i + n; j < len(b); {
			if m := run(j); m == n {
				end = j + m
				break
			} else if m > 0 {
				j += m
			} else {
				j++
			}
		}
		if end < 0 { // unclosed: the backticks are text
			i += n
			continue
		}
		for k := i; k < end; k++ {
			if b[k] != '\n' {
				b[k] = ' '
			}
		}
		i = end
	}
	return string(b)
}

// yamlScalar is a frontmatter value as YAML reads it: "double" (escapes),
// 'single' (” is '), or plain.
func yamlScalar(value string) string {
	v := strings.TrimSpace(value)
	switch {
	case strings.HasPrefix(v, `"`):
		for i := 1; i < len(v); i++ {
			if v[i] == '\\' {
				i++
			} else if v[i] == '"' {
				if t, err := strconv.Unquote(v[:i+1]); err == nil {
					return t
				}
				break
			}
		}
	case strings.HasPrefix(v, "'"):
		for i := 1; i < len(v); i++ {
			if v[i] == '\'' && i+1 < len(v) && v[i+1] == '\'' {
				i++
			} else if v[i] == '\'' {
				return strings.ReplaceAll(v[1:i], "''", "'")
			}
		}
	}
	if at := strings.Index(v, " #"); at >= 0 { // a comment; "C#" isn't one
		v = strings.TrimSpace(v[:at])
	}
	return v
}

// page is a note's text: frontmatter (with extra lines), heading, body and sections.
func page(title, kind, extra, body, sections string) string {
	return fmt.Sprintf("---\ntitle: %q\ntype: %s\npermalink: %s\n%stags: [design]\n---\n\n# %s\n\n%s%s", title, kind, permalink(title), extra, title, body, sections)
}

// permalink is Basic Memory's: the title in lower kebab case.
func permalink(title string) string {
	var b strings.Builder
	dash := false
	for _, r := range strings.ToLower(title) {
		if r >= 'a' && r <= 'z' || r >= '0' && r <= '9' || r > 127 {
			b.WriteRune(r)
			dash = false
		} else if !dash && b.Len() > 0 {
			b.WriteByte('-')
			dash = true
		}
	}
	return strings.TrimSuffix(b.String(), "-")
}

// fileTitle is name as a note's title and file name: without what file
// systems or [[links]] can't hold.
func fileTitle(name string) string {
	name = strings.Map(func(r rune) rune {
		if strings.ContainsRune(`/\:*?"<>|#^[]`, r) || r < 32 {
			return -1
		}
		return r
	}, strings.TrimSuffix(strings.TrimSpace(name), ".md"))
	return strings.Trim(strings.TrimSpace(name), ".")
}

func oneLine(s string) string { return strings.Join(strings.Fields(s), " ") }

// write saves text as rel (a path from root), never through a link: one
// could lead out of the game.
func write(root, rel, text string) error {
	path := filepath.Join(root, filepath.FromSlash(rel))
	realRoot, err := filepath.EvalSymlinks(root)
	if err != nil {
		return err
	}
	existing := filepath.Dir(path) // checked before folders are made in it: MkdirAll follows links
	for _, err := os.Lstat(existing); err != nil && existing != root; _, err = os.Lstat(existing) {
		existing = filepath.Dir(existing)
	}
	if realExisting, err := filepath.EvalSymlinks(existing); err != nil {
		return err
	} else if within, err := filepath.Rel(realRoot, realExisting); err != nil || !filepath.IsLocal(within) && within != "." {
		return fmt.Errorf("%s is outside the game", rel)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	realDir, err := filepath.EvalSymlinks(filepath.Dir(path))
	if err != nil {
		return err
	}
	if within, err := filepath.Rel(realRoot, realDir); err != nil || !filepath.IsLocal(within) {
		return fmt.Errorf("%s is outside the game", rel)
	}
	if info, err := os.Lstat(path); err == nil && info.Mode()&os.ModeSymlink != 0 {
		return fmt.Errorf("%s is a link: not written through", rel)
	}
	return atomicfile.WriteFile(path, []byte(text), 0o644)
}

func index(notes ...string) string {
	var b strings.Builder
	b.WriteString("\n## Relations\n")
	for _, n := range notes {
		fmt.Fprintf(&b, "- includes [[%s]]\n", n)
	}
	return b.String()
}

const partOfDesign = "\n## Observations\n\n## Relations\n- part_of [[Design]]\n"

var scaffold = []struct{ path, text string }{
	{"Design.md", page("Design", "index", "", "What this game is and why, for the person and their agent: the game's memory. "+
		"Obsidian opens this folder as a vault; `jm memory` reads and adds to it.\n", index("Pitch", "Pillars", "Levels", "Systems", "Decisions", "Notes"))},
	{"Pitch.md", page("Pitch", "note", "", "The game in a few sentences: who you are, what you do, and why it's fun.\n", partOfDesign)},
	{"Pillars.md", page("Pillars", "note", "", "Three or so things every change should serve (e.g. `- [pillar] jumps feel exact`).\n", partOfDesign)},
	{"Levels.md", page("Levels", "note", "", "Each level or area: its idea, what it teaches, how it should feel, and its scene (`scenes/main.scene.json`).\n", partOfDesign)},
	{"Systems.md", page("Systems", "note", "", "The rules: movement, combat, items, progress. What each is for and its numbers.\n", partOfDesign)},
	{"Decisions.md", page("Decisions", "index", "", "What was decided and why, a note each in decisions/ (`jm memory decide`).\n",
		"\n## Relations\n- part_of [[Design]]\n")},
	{"Notes.md", page("Notes", "note", "", "What comes up along the way: playtests, feelings, ideas (`jm memory note`).\n", partOfDesign)},
	// Obsidian's settings: links kept up to date when a note is renamed; its window layout stays out of git.
	{".obsidian/app.json", "{\n  \"alwaysUpdateLinks\": true,\n  \"newLinkFormat\": \"shortest\"\n}\n"},
	{".obsidian/.gitignore", "workspace*.json\n"},
}
