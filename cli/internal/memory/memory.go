// Package memory is a game's design notes and what was decided: design/, a
// folder of Markdown notes that Obsidian opens as a vault and Basic Memory
// can index (a note per thing: frontmatter, observations "- [category] text",
// relations "- kind [[Note]]").
package memory

import (
	"cmp"
	"errors"
	"fmt"
	"io/fs"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"slices"
	"sort"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
	"github.com/Jumballaya/Journeyman-Engine/internal/realpath"
)

// Dir is the notes' folder in a game.
const Dir = "design"

// Note is one note. Path is from the game's folder, with slashes; Links are
// the [[targets]] it names, each once, in order.
type Note struct {
	Title        string         `json:"title"`
	Path         string         `json:"path"`
	Type         string         `json:"type,omitempty"`
	Props        map[string]any `json:"props,omitempty"` // its other frontmatter properties
	Observations []Observation  `json:"observations"`
	Links        []string       `json:"links"`
	Relations    []Relation     `json:"relations,omitempty"` // the typed ones it states
	Text         string         `json:"-"`
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
		if f.text == artNote && hasArtNote(root) { // a blank Art.md would win over the game's own art note
			continue
		}
		if err := write(root, Dir+"/"+f.path, f.text); err != nil {
			return made, err
		}
		made = append(made, Dir+"/"+f.path)
	}
	return made, nil
}

// hasArtNote is whether the game has a note of type art (or may: one it can't read).
func hasArtNote(root string) bool {
	all, unread := notes(root)
	return unread != nil || slices.ContainsFunc(all, func(n Note) bool { return n.Type == "art" })
}

// Notes are the game's notes, sorted by path; none without design/.
func Notes(root string) []Note {
	out, _ := notes(root)
	return out
}

// notes is Notes, and the first note it couldn't read (left out): a change mustn't take that one for missing.
func notes(root string) ([]Note, error) {
	var out []Note
	var unread error
	root, _ = filepath.Abs(root)
	dir := filepath.Join(root, Dir) // design/ may link to a folder in the game; one leading out isn't read
	if real, rel, ok := realpath.Rel(root, Dir); ok && rel != "." {
		dir = real
	}
	walked := filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			if path != dir || !errors.Is(err, fs.ErrNotExist) { // no design/ is no notes; a folder it can't list may hide one
				unread = cmp.Or(unread, err)
			}
			return nil
		}
		if d.IsDir() && path != dir && (strings.HasPrefix(d.Name(), ".") || path == filepath.Join(dir, Templates)) {
			return filepath.SkipDir // .obsidian (design/ itself may lead to a hidden folder); templates aren't notes
		}
		if !d.Type().IsRegular() || !strings.HasSuffix(path, ".md") {
			return nil
		}
		data, err := os.ReadFile(path)
		if err != nil {
			unread = cmp.Or(unread, err)
			return nil
		}
		rel, _ := filepath.Rel(dir, path)
		out = append(out, parse(Dir+"/"+filepath.ToSlash(rel), string(data)))
		return nil
	})
	sort.Slice(out, func(i, j int) bool { return out[i].Path < out[j].Path })
	return out, cmp.Or(unread, walked)
}

// ErrNoNote: no note is the one named.
var ErrNoNote = errors.New("no note")

// Find is the one note ref means: the note at its path (design/North/Harbor.md),
// else the one it names as a [[ref]] would. ErrNoNote when none is; an error
// naming each by its path when several are (two Harbors: say which).
func Find(root, ref string) (Note, error) {
	switch found := named(Notes(root), ref); len(found) {
	case 0:
		return Note{}, fmt.Errorf("%w %q (memory lists them)", ErrNoNote, strings.TrimSpace(ref))
	case 1:
		return found[0], nil
	default:
		var paths []string
		for _, n := range found {
			paths = append(paths, n.Path)
		}
		return Note{}, fmt.Errorf("%q is %d notes: say which by its path (%s)", strings.TrimSpace(ref), len(found), strings.Join(paths, ", "))
	}
}

// named are the notes ref may mean: the one at its path (.md optional) alone,
// else those at it in any case (one, on a case-blind disk), else each a
// [[ref]] names (title, file name or folder/title).
func named(all []Note, ref string) []Note {
	ref = strings.TrimSuffix(strings.TrimSpace(ref), ".md")
	var found []Note
	for _, n := range all {
		switch path := strings.TrimSuffix(n.Path, ".md"); {
		case path == ref:
			return []Note{n}
		case strings.EqualFold(path, ref):
			found = append(found, n)
		}
	}
	if len(found) > 0 {
		return found
	}
	for _, n := range all {
		if linksTo(ref, n) {
			found = append(found, n)
		}
	}
	return found
}

// NameOf is the shortest name a [[link]] finds n alone by: its title, else its
// file name, else its folder/file, else its path.
func NameOf(all []Note, n Note) string {
	stem := strings.TrimSuffix(strings.TrimPrefix(n.Path, Dir+"/"), ".md")
	for _, name := range []string{n.Title, path.Base(stem), stem} {
		if strings.ContainsAny(name, "[]|#^\n") { // a [[link]] of it would end or split there
			continue
		}
		if found := named(all, name); len(found) == 1 && found[0].Path == n.Path {
			return name
		}
	}
	return n.Path
}

// A Ref is a note as another shows it: by its title, found by its path.
type Ref struct {
	Title string `json:"title"`
	Path  string `json:"path"`
}

// Backlinks are the notes that link to n.
func Backlinks(root string, n Note) []Ref {
	out := []Ref{}
	for _, other := range Notes(root) {
		for _, l := range other.Links {
			if other.Path != n.Path && linksTo(l, n) {
				out = append(out, Ref{other.Title, other.Path})
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
	n, err := Find(root, note)
	if errors.Is(err, ErrNoNote) {
		n, err = Find(root, title)
	}
	if !errors.Is(err, ErrNoNote) && err != nil { // two of that name: the one meant isn't guessed
		return Note{}, err
	}
	if _, unread := notes(root); err != nil && unread != nil {
		return Note{}, fmt.Errorf("a note can't be read (%v): it may be the one meant, so nothing was written", unread)
	}
	if err != nil {
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
	index, err := Find(root, Dir+"/Decisions.md") // the scaffold's: another titled Decisions isn't the index
	indexed := err == nil
	if n, err := Find(root, title); !errors.Is(err, ErrNoNote) {
		if err != nil {
			return Note{}, err
		}
		if n.Path != path || !indexed || slices.ContainsFunc(index.Links, func(l string) bool { return linksTo(l, n) }) {
			return Note{}, fmt.Errorf("%s already exists: add to it with a note instead", n.Path)
		}
		return n, addToIndex(root, index, title) // a decision whose index update failed: finish it
	}
	if _, unread := notes(root); unread != nil {
		return Note{}, fmt.Errorf("a note can't be read (%v): it may be this decision or the index, so nothing was written", unread)
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
	if err := write(root, index.Path, addToSection(index.Text, "Relations", "- has_part [["+title+"]]")); err != nil {
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
	front, body, _ := frontmatter(text)
	for _, p := range front {
		switch p.key {
		case "title":
			n.Title = p.text()
		case "type":
			n.Type = p.text()
		}
	}
	if props := propsOf(front); len(props) > 0 {
		n.Props = props
	}
	if n.Title == "" {
		n.Title = strings.TrimSuffix(filepath.Base(path), ".md")
	}
	seen := map[string]bool{}
	link := func(line string) {
		for _, m := range wikilink.FindAllStringSubmatch(line, -1) {
			if target := strings.TrimSpace(m[1]); !seen[target] {
				seen[target] = true
				n.Links = append(n.Links, target)
			}
		}
	}
	for _, p := range front { // a property's [[link]] is one, as Obsidian has it
		if values, _ := p.values(); p.key != "title" {
			for _, v := range values {
				link(v)
			}
		}
	}
	lines := strings.Split(body, "\n")
	for i, line := range prose(lines) {
		// Its marker found outside code; its text as written, after the marker (prose keeps every column).
		if m := observation.FindStringSubmatchIndex(line); m != nil {
			n.Observations = append(n.Observations, Observation{lines[i][m[2]:m[3]], strings.TrimSpace(lines[i][m[3]+1:])})
		}
		link(line)
		if r, ok := relationOf(line, lines[i]); ok {
			n.Relations = append(n.Relations, r)
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
	for i, block := range codeBlocks(lines) {
		out[i] = block > 0
	}
	return out
}

// codeBlocks numbers each code block's lines (fenced as above, or indented) from 1; 0 isn't code.
func codeBlocks(lines []string) []int {
	out, n, goesOn := make([]int, len(lines)), 0, false // goesOn: this line is in a block that goes on after it
	fence := ""                                         // the open block's fence
	blank, inList := true, false                        // the line before is blank (or none); the last text was a list's
	for i, l := range lines {
		wasOn := goesOn
		goesOn = false
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
		case fence != "": // in a block: its lines are code, and say nothing about lists
			out[i], goesOn = n, true
			if mark != "" && mark[0] == fence[0] && len(mark) >= len(fence) && strings.TrimSpace(t[len(mark):]) == "" {
				fence, goesOn = "", false
			}
		case isIndentedCode(l) && (blank || i > 0 && out[i-1] > 0) && !inList:
			if !wasOn {
				n++
			}
			out[i], goesOn = n, true
		case strings.TrimSpace(l) == "":
			blank = true
			continue
		case mark != "":
			n++
			out[i], fence, goesOn = n, mark, true
			if len(l)-len(t) < 2 {
				inList = false // a fence of its own ends a list; one indented under an item is the item's
			}
		case listItem.MatchString(l):
			inList = true
		case blank && !isIndentedCode(l):
			inList = false // text after a blank line, not indented under the list
		}
		blank = false
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
	outside := fmt.Errorf("%s is outside the game", rel)
	if !realpath.In(root, filepath.FromSlash(rel)) { // checked before folders are made in it: MkdirAll follows links
		return outside
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	if !realpath.In(root, filepath.FromSlash(rel)) { // a folder on the way made a link meanwhile
		return outside
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
		fmt.Fprintf(&b, "- has_part [[%s]]\n", n)
	}
	return b.String()
}

// artNote is the game's art direction: what a sprite brief asks (palette,
// grid, view, light, outline) answered once for every sprite, each kind's
// frame size, and each animation set's tags. jm build holds the art to it.
const artNote = `---
title: "Art"
type: art
permalink: art
palette:
grid:
view:
light:
outline:
references: []
sizes:
  # a dossier type or animation set: its frame, e.g. character: 32x32
animations:
  # a set: its directions, and each action's frames' milliseconds (or a frame
  # count); tagged <action>_<direction>, an undirected action by its name. e.g.
  # character:
  #   directions: [down, up, left]
  #   undirected: [down]
  #   idle: [400, 400]
  #   down: [120, 120, 120, 600]
tags: [design, art]
---

# Art

How the game looks, for whoever draws its sprites, tiles and maps. The
properties above are the locks every picture keeps: the palette file
(` + "`.gpl`, `.hex` or `.pal`" + ` in the game), the grid, the view and light, the
outline colour (a palette colour's name), each kind's frame size, each
animation set. A dossier with a ` + "`sprite`" + ` is
drawn by them (` + "`art_set`" + ` picks a set other than its type; ` + "`art: done`" + `
says it's finished); ` + "`references`" + ` are finished sprites new ones match.

## Palette

Its ramps by name, darkest to lightest, and what each is for.

## Shading

Light, shadows, anti-aliasing, dithering.

## Visual language

Each place's and faction's look: colours, shapes, materials.
` + partOfDesign

const partOfDesign = "\n## Observations\n\n## Relations\n- part_of [[Design]]\n"

var scaffold = []struct{ path, text string }{
	{"Design.md", page("Design", "index", "", "What this game is and why, for the person and their agent: the game's memory. "+
		"Obsidian opens this folder as a vault; `jm memory` reads and adds to it.\n", index("Pitch", "Pillars", "Levels", "Systems", "Art", "Decisions", "Notes"))},
	{"Pitch.md", page("Pitch", "note", "", "The game in a few sentences: who you are, what you do, and why it's fun.\n", partOfDesign)},
	{"Pillars.md", page("Pillars", "note", "", "Three or so things every change should serve (e.g. `- [pillar] jumps feel exact`).\n", partOfDesign)},
	{"Levels.md", page("Levels", "note", "", "Each level or area: its idea, what it teaches, how it should feel, and its scene (`scenes/main.scene.json`).\n", partOfDesign)},
	{"Systems.md", page("Systems", "note", "", "The rules: movement, combat, items, progress. What each is for and its numbers.\n", partOfDesign)},
	{"Art.md", artNote},
	{"Decisions.md", page("Decisions", "index", "", "What was decided and why, a note each in decisions/ (`jm memory decide`).\n",
		"\n## Relations\n- part_of [[Design]]\n")},
	{"Notes.md", page("Notes", "note", "", "What comes up along the way: playtests, feelings, ideas (`jm memory note`).\n", partOfDesign)},
	// Obsidian's settings: links kept up to date when a note is renamed; its window layout stays out of git.
	{".obsidian/app.json", "{\n  \"alwaysUpdateLinks\": true,\n  \"newLinkFormat\": \"shortest\"\n}\n"},
	{".obsidian/.gitignore", "workspace*.json\n"},
}
