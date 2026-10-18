package memory

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
)

// Templates is the folder of dossier templates: one note per kind, named
// for the folder its dossiers go in (Characters.md: design/Characters/).
// Its frontmatter is the kind's properties, with their defaults; its body
// is where a new dossier starts ({{title}}, {{summary}}: as Obsidian's
// Templates plugin fills them). A game edits them, or adds a kind with a
// file of its own.
const Templates = "_templates"

// Kind is a sort of dossier: a character, an item, a place.
type Kind struct {
	Name   string   `json:"name"`   // its notes' type: character
	Folder string   `json:"folder"` // where they go: Characters
	Props  []string `json:"props"`  // its properties, in order
}

// builtin are the kinds every game has, as their templates start; a game's
// own template of the same folder replaces one.
var builtin = []struct{ folder, text string }{
	{"Characters", template("character", "role, faction, home, portrait", "Appearance", "Personality", "History")},
	{"Items", template("item", "category, rarity, value, owner, icon", "Description", "Use", "Origin")},
	{"Locations", template("location", "region, ruler, population, map", "Description", "People", "History")},
	{"Factions", template("faction", "leader, seat, allies, rivals", "Goals", "Members", "History")},
	{"Quests", template("quest", "giver, location, reward, status", "Setup", "Steps", "Outcome")},
	{"Lore", template("lore", "era, related", "Account", "Sources")},
}

// template is a built-in template: its properties, empty, and its sections.
func template(kind, props string, sections ...string) string {
	var b strings.Builder
	fmt.Fprintf(&b, "---\ntype: %s\n", kind)
	for _, p := range strings.Split(props, ", ") {
		fmt.Fprintf(&b, "%s: \n", p)
	}
	fmt.Fprintf(&b, "tags: [%s]\n---\n\n# {{title}}\n\n{{summary}}\n", kind)
	for _, s := range sections {
		fmt.Fprintf(&b, "\n## %s\n", s)
	}
	b.WriteString("\n## Observations\n\n## Relations\n")
	return b.String()
}

// Kinds are the game's dossier kinds: the built-in ones and its own, by folder.
func Kinds(root string) []Kind {
	var out []Kind
	for _, f := range kindFiles(root) {
		out = append(out, kindOf(f.folder, f.text))
	}
	slices.SortFunc(out, func(a, b Kind) int { return strings.Compare(a.Folder, b.Folder) })
	return out
}

type kindFile struct{ folder, text string }

// kindFiles are the templates: the game's, and the built-in ones it has no file for.
func kindFiles(root string) []kindFile {
	var out []kindFile
	own := map[string]bool{}
	entries, _ := os.ReadDir(filepath.Join(root, Dir, Templates))
	for _, e := range entries {
		folder, ok := strings.CutSuffix(e.Name(), ".md")
		if !ok || !e.Type().IsRegular() || fileTitle(folder) != folder || strings.HasPrefix(folder, "_") {
			continue // its dossiers' folder would be no name, or the templates' own
		}
		if data, err := os.ReadFile(filepath.Join(root, Dir, Templates, e.Name())); err == nil {
			out = append(out, kindFile{folder, strings.ReplaceAll(string(data), "\r\n", "\n")})
			own[strings.ToLower(folder)] = true
		}
	}
	for _, b := range builtin {
		if !own[strings.ToLower(b.folder)] {
			out = append(out, kindFile{b.folder, b.text})
		}
	}
	return out
}

func kindOf(folder, text string) Kind {
	k := Kind{Folder: folder, Props: []string{}}
	front, _, _ := frontmatter(text)
	for _, p := range front {
		switch p.key {
		case "type":
			k.Name = yamlScalar(p.value)
		case "title", "permalink", "tags":
		default:
			k.Props = append(k.Props, p.key)
		}
	}
	if k.Name == "" {
		k.Name = strings.ToLower(folder)
	}
	return k
}

// NewDossier writes a dossier of a kind (its type or folder, any case):
// design/<Folder>/<name>.md from the kind's template, with props set and the
// summary under its heading. The first of a kind also writes its template
// into the game (to edit) and <Folder>.base, the kind as a table in Obsidian.
func NewDossier(root, kind, name, summary string, props map[string]string) (Note, error) {
	title := fileTitle(name)
	if title == "" {
		return Note{}, fmt.Errorf("%q can't be a dossier's name", name)
	}
	var file *kindFile
	for _, f := range kindFiles(root) {
		k := kindOf(f.folder, f.text)
		if strings.EqualFold(k.Name, kind) || strings.EqualFold(k.Folder, kind) {
			file = &f
			break
		}
	}
	if file == nil {
		var names []string
		for _, k := range Kinds(root) {
			names = append(names, k.Name)
		}
		return Note{}, fmt.Errorf("no kind %q: one of %s, or a template of your own in %s/%s/", kind, strings.Join(names, ", "), Dir, Templates)
	}
	for key := range props {
		if !propertyName.MatchString(key) {
			return Note{}, fmt.Errorf("%q can't be a property's name: letters, digits, spaces, - and _", key)
		}
		if key == "title" || key == "type" || key == "permalink" {
			return Note{}, fmt.Errorf("%s is the dossier's own: its name and kind set it", key)
		}
	}
	release, err := gamelock.Hold()
	if err != nil {
		return Note{}, err
	}
	defer release()
	if _, err := scaffoldHeld(root); err != nil { // first: a dossier can't take a design note's name
		return Note{}, err
	}
	path := Dir + "/" + file.folder + "/" + title + ".md"
	if info, err := os.Lstat(filepath.Join(root, Dir, file.folder)); err == nil && info.Mode()&os.ModeSymlink != 0 {
		return Note{}, fmt.Errorf("%s/%s is a link: the notes there aren't read, so none is written there", Dir, file.folder)
	}
	if n, ok := Find(root, title); ok {
		return Note{}, fmt.Errorf("%s already exists: add to it instead", n.Path)
	}
	if _, err := os.Stat(filepath.Join(root, filepath.FromSlash(path))); !errors.Is(err, os.ErrNotExist) {
		return Note{}, fmt.Errorf("%s is there already (or can't be checked): nothing was written", path) // a folder through a link isn't listed
	}
	if _, unread := notes(root); unread != nil {
		return Note{}, fmt.Errorf("a note can't be read (%v): it may be this one, so nothing was written", unread)
	}
	k := kindOf(file.folder, file.text)
	for _, own := range []struct{ rel, text string }{
		{Dir + "/" + Templates + "/" + file.folder + ".md", file.text},
		{Dir + "/" + file.folder + ".base", base(k)},
	} {
		if _, err := os.Lstat(filepath.Join(root, filepath.FromSlash(own.rel))); errors.Is(err, os.ErrNotExist) {
			if err := write(root, own.rel, own.text); err != nil {
				return Note{}, err
			}
		}
	}
	text := fill(file.text, k.Name, title, strings.TrimSpace(summary), props)
	if err := write(root, path, text); err != nil {
		return Note{}, err
	}
	return parse(path, text), nil
}

// fill is a template made a dossier: its frontmatter with the title,
// permalink and props set (ones it doesn't name added), {{title}} and
// {{summary}} replaced.
func fill(text, kind, title, summary string, props map[string]string) string {
	front, body, ok := frontmatter(text)
	if !ok {
		body = text
	}
	set := map[string]bool{}
	var b strings.Builder
	fmt.Fprintf(&b, "---\ntitle: %q\n", title)
	if !slices.ContainsFunc(front, func(p property) bool { return p.key == "type" }) {
		fmt.Fprintf(&b, "type: %s\n", kind)
	}
	for _, p := range front {
		switch v, given := props[p.key]; {
		case p.key == "title" || p.key == "permalink":
			continue
		case given:
			fmt.Fprintf(&b, "%s: %s\n", p.key, yamlValue(v))
			set[p.key] = true
		default:
			b.WriteString(p.raw + "\n")
		}
	}
	keys := make([]string, 0, len(props))
	for key := range props {
		keys = append(keys, key)
	}
	slices.Sort(keys)
	for _, key := range keys {
		if !set[key] && key != "title" && key != "type" && key != "permalink" {
			fmt.Fprintf(&b, "%s: %s\n", key, yamlValue(props[key]))
		}
	}
	fmt.Fprintf(&b, "permalink: %s\n---\n", permalink(title))
	body = strings.ReplaceAll(body, "{{title}}", title)
	body = strings.ReplaceAll(body, "{{summary}}", summary)
	return b.String() + body
}

// propertyName is a name a property can have here: one Obsidian and YAML
// both read as written.
var propertyName = regexp.MustCompile(`^[\pL\pN_][\pL\pN _-]*$`)

// yamlValue is a property's value in frontmatter: quoted, so a [[link]] or a
// colon stays text (Obsidian reads a quoted [[link]] as one).
func yamlValue(v string) string {
	if v = oneLine(v); v == "" {
		return `""`
	}
	return fmt.Sprintf("%q", v)
}

// base is a kind's Obsidian base: its notes as a table of its properties.
func base(k Kind) string {
	var b strings.Builder
	fmt.Fprintf(&b, "filters:\n  and:\n    - %s\n    - '!file.inFolder(\"%s\")'\nviews:\n  - type: table\n    name: %q\n    order:\n      - file.name\n",
		quoted(fmt.Sprintf("note.type == %q", k.Name)), Templates, k.Folder)
	for _, p := range k.Props {
		fmt.Fprintf(&b, "      - note.%s\n", p)
	}
	return b.String()
}

// quoted is s in YAML's single quotes (' doubled).
func quoted(s string) string { return "'" + strings.ReplaceAll(s, "'", "''") + "'" }

// property is a frontmatter line "key: value" (raw: as written, with the
// lines of a block list under it).
type property struct{ key, value, raw string }

// frontmatter splits a note's frontmatter into properties, and its body.
func frontmatter(text string) ([]property, string, bool) {
	rest, ok := strings.CutPrefix(text, "---\n")
	if !ok {
		return nil, text, false
	}
	front, body, ok := strings.Cut(rest, "\n---\n")
	if !ok {
		return nil, text, false
	}
	var out []property
	for _, line := range strings.Split(front, "\n") {
		if key, value, found := strings.Cut(line, ":"); found && line != "" && line[0] != ' ' && line[0] != '-' && line[0] != '#' {
			out = append(out, property{strings.TrimSpace(key), value, line})
		} else if len(out) > 0 && strings.TrimSpace(line) != "" {
			out[len(out)-1].raw += "\n" + line // a block list's item
		}
	}
	return out, body, true
}

// propsOf are a note's frontmatter properties as tools show them: text, or a
// list's items. title, type and permalink are the note's own fields.
func propsOf(front []property) map[string]any {
	out := map[string]any{}
	for _, p := range front {
		if p.key == "title" || p.key == "type" || p.key == "permalink" {
			continue
		}
		if items, list := p.values(); list {
			out[p.key] = items
		} else {
			out[p.key] = items[0]
		}
	}
	return out
}

// values are a property's value as YAML reads it: one, or a list's items
// ([a, "b, c"] or "- a" lines under it).
func (p property) values() ([]string, bool) {
	v := strings.TrimSpace(p.value)
	if strings.HasPrefix(v, "#") {
		v = "" // only a comment
	}
	switch {
	case strings.HasPrefix(v, "[") && !strings.HasPrefix(v, "[["):
		items := []string{}
		lines := strings.Split(p.raw, "\n") // a list may go on over lines
		for _, item := range flowItems(strings.Join(append([]string{v}, lines[1:]...), "\n")) {
			if item = yamlScalar(item); item != "" {
				items = append(items, item)
			}
		}
		return items, true
	case v == "" && strings.Contains(p.raw, "\n"):
		items := []string{}
		for _, line := range strings.Split(p.raw, "\n")[1:] {
			if item, ok := strings.CutPrefix(strings.TrimSpace(line), "- "); ok {
				items = append(items, yamlScalar(item))
			}
		}
		return items, true
	}
	return []string{yamlScalar(v)}, false
}

// flowItems are a [flow, list]'s items as written, split at the commas
// outside quotes, up to its closing bracket, without # comments. A quote
// opens one only at an item's start: King's Guard is plain text.
func flowItems(v string) []string {
	var items []string
	quote, start := byte(0), 1
	for i := 1; i < len(v); i++ {
		switch c := v[i]; {
		case quote == '"' && c == '\\', quote == '\'' && c == '\'' && i+1 < len(v) && v[i+1] == '\'':
			i++ // an escape, or '' (a ' in single quotes)
		case quote != 0:
			if c == quote {
				quote = 0
			}
		case c == '#' && (v[i-1] == ' ' || v[i-1] == '\t' || v[i-1] == '\n'):
			if end := strings.IndexByte(v[i:], '\n'); end >= 0 {
				v = v[:i] + v[i+end:] // the comment taken out
			} else {
				v = v[:i]
			}
			i--
		case (c == '"' || c == '\'') && strings.TrimSpace(v[start:i]) == "":
			quote = c
		case c == ',' || c == ']':
			items = append(items, v[start:i])
			start = i + 1
			if c == ']' {
				return items
			}
		}
	}
	return items
}
