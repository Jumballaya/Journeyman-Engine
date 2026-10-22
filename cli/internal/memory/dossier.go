package memory

import (
	"cmp"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
	"github.com/Jumballaya/Journeyman-Engine/internal/realpath"
	"gopkg.in/yaml.v3"
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
	{"Characters", template("character", "role, faction, home, portrait, sprite, art", "Appearance", "Personality", "History")},
	{"Creatures", template("creature", "habitat, faction, threat, sprite, art", "Appearance", "Behavior", "Lore")},
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
	files, _ := kindFiles(root) // a template that can't be read isn't listed
	for _, f := range files {
		out = append(out, kindOf(f.folder, f.text))
	}
	slices.SortFunc(out, func(a, b Kind) int { return strings.Compare(a.Folder, b.Folder) })
	return out
}

type kindFile struct{ folder, text string }

// kindFiles are the templates: the game's, and the built-in ones it has no
// file for. An error: one of the game's (or its folder) can't be read.
func kindFiles(root string) ([]kindFile, error) {
	var out []kindFile
	own := map[string]bool{}
	dir := filepath.Join(root, Dir, Templates)
	entries, failed := os.ReadDir(dir)
	if errors.Is(failed, os.ErrNotExist) {
		failed = nil
	} else if failed == nil && !realpath.In(root, filepath.Join(Dir, Templates)) {
		entries, failed = nil, fmt.Errorf("%s/%s leads out of the game", Dir, Templates)
	}
	for _, e := range entries {
		folder, ok := strings.CutSuffix(e.Name(), ".md")
		if !ok || !e.Type().IsRegular() || fileTitle(folder) != folder || strings.HasPrefix(folder, "_") {
			continue // its dossiers' folder would be no name, or the templates' own
		}
		data, err := os.ReadFile(filepath.Join(dir, e.Name()))
		if err != nil {
			failed = cmp.Or(failed, err)
			continue
		}
		out = append(out, kindFile{folder, strings.ReplaceAll(string(data), "\r\n", "\n")})
		own[strings.ToLower(folder)] = true
	}
	for _, b := range builtin {
		if !own[strings.ToLower(b.folder)] {
			out = append(out, kindFile{b.folder, b.text})
		}
	}
	return out, failed
}

func kindOf(folder, text string) Kind {
	k := Kind{Folder: folder, Props: []string{}}
	front, _, _ := frontmatter(text)
	for _, p := range front {
		switch p.key {
		case "type":
			k.Name = p.text()
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
	for key := range props {
		if !propertyName.MatchString(key) {
			return Note{}, fmt.Errorf("%q can't be a property's name: letters, digits, spaces (not at its ends), - and _", key)
		}
		if k := strings.ToLower(key); k == "title" || k == "type" || k == "permalink" {
			return Note{}, fmt.Errorf("%s is the dossier's own: its name and kind set it", key)
		}
	}
	release, err := gamelock.Hold()
	if err != nil {
		return Note{}, err
	}
	defer release()
	files, err := kindFiles(root) // under the lock: the template it's made from is the one there now
	if err != nil {
		return Note{}, fmt.Errorf("the templates in %s/%s/ can't be read (%v): nothing was written", Dir, Templates, err)
	}
	var file *kindFile
	for _, f := range files {
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
	if _, _, err := frontmatter(file.text); err != nil {
		return Note{}, fmt.Errorf("%s/%s/%s.md: %v", Dir, Templates, file.folder, err)
	}
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
	text := fill(file.text, k.Name, title, strings.TrimSpace(summary), props)
	if _, _, err := frontmatter(text); err != nil { // an indented template's, or its alias of a property set
		return Note{}, fmt.Errorf("%s/%s/%s.md filled in %v: nothing was written", Dir, Templates, file.folder, err)
	}
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
	if err := write(root, path, text); err != nil {
		return Note{}, err
	}
	return parse(path, text), nil
}

// fill is a template made a dossier: its frontmatter with the title, type,
// permalink and props set (ones it doesn't name added), {{title}} and
// {{summary}} replaced.
func fill(text, kind, title, summary string, props map[string]string) string {
	front, body, _ := frontmatter(text)
	set := map[string]bool{}
	var b strings.Builder
	fmt.Fprintf(&b, "---\ntitle: %q\ntype: %s\n", title, yamlText(kind)) // its kind, as its base looks for it
	for _, p := range front {
		switch v, given := props[p.key]; {
		case p.key == "title" || p.key == "type" || p.key == "permalink":
			continue
		case given:
			fmt.Fprintf(&b, "%s: %s\n", yamlText(p.key), yamlValue(v))
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
			fmt.Fprintf(&b, "%s: %s\n", yamlText(key), yamlValue(props[key]))
		}
	}
	fmt.Fprintf(&b, "permalink: %s\n---\n", permalink(title))
	body = strings.ReplaceAll(body, "{{title}}", title)
	body = strings.ReplaceAll(body, "{{summary}}", summary)
	return b.String() + body
}

// propertyName is a name a property can have here: one Obsidian and YAML
// both read as written.
var propertyName = regexp.MustCompile(`^[\pL\pN_](?:[\pL\pN _-]*[\pL\pN_-])?$`)

// yamlValue is a property's value in frontmatter: quoted, so a [[link]] or a
// colon stays text (Obsidian reads a quoted [[link]] as one).
func yamlValue(v string) string {
	if v = oneLine(v); v == "" {
		return `""`
	}
	return fmt.Sprintf("%q", v)
}

// yamlText is s as a YAML string: plain when it reads back as written, else quoted.
func yamlText(s string) string {
	out, _ := yaml.Marshal(&yaml.Node{Kind: yaml.ScalarNode, Tag: "!!str", Value: s})
	return strings.TrimSuffix(string(out), "\n")
}

// base is a kind's Obsidian base: its notes as a table of its properties.
func base(k Kind) string {
	var b strings.Builder
	fmt.Fprintf(&b, "filters:\n  and:\n    - %s\n    - '!file.inFolder(\"%s\")'\nviews:\n  - type: table\n    name: %q\n    order:\n      - file.name\n",
		quoted(fmt.Sprintf("note.type == %q", k.Name)), Templates, k.Folder)
	for _, p := range k.Props {
		fmt.Fprintf(&b, "      - %s\n", yamlText("note."+p))
	}
	return b.String()
}

// quoted is s in YAML's single quotes (' doubled).
func quoted(s string) string { return "'" + strings.ReplaceAll(s, "'", "''") + "'" }

// property is a frontmatter property: its name as YAML reads it, its value,
// and its lines as written (raw: to write it again unchanged).
type property struct {
	key  string
	node *yaml.Node
	raw  string
}

// frontmatter splits a note's text into its frontmatter's properties and its
// body (all of text, when it has none). An error: YAML can't read them as a
// property a line, each once.
func frontmatter(text string) ([]property, string, error) {
	rest, ok := strings.CutPrefix(text, "---\n")
	if !ok {
		return nil, text, nil
	}
	front, body, ok := strings.Cut(rest, "\n---\n")
	if !ok {
		return nil, text, nil
	}
	var doc yaml.Node
	if err := yaml.Unmarshal([]byte(front), &doc); err != nil {
		return nil, body, fmt.Errorf("its frontmatter isn't YAML: %w", err)
	}
	if len(doc.Content) == 0 {
		return nil, body, nil // empty
	}
	if m := doc.Content[0]; m.Kind != yaml.MappingNode || m.Style&yaml.FlowStyle != 0 {
		return nil, body, errors.New("its frontmatter isn't properties, one a line")
	}
	lines := strings.Split(front, "\n")
	var out []property
	var starts []int
	m := doc.Content[0].Content
	for i := 0; i+1 < len(m); i += 2 {
		key := m[i]
		if key.Kind != yaml.ScalarNode || slices.ContainsFunc(out, func(p property) bool { return p.key == key.Value }) ||
			len(starts) > 0 && key.Line-1 <= starts[len(starts)-1] {
			return nil, body, fmt.Errorf("its frontmatter has %q twice, or not on a line of its own", key.Value)
		}
		out = append(out, property{key: key.Value, node: m[i+1]})
		starts = append(starts, key.Line-1)
	}
	for i := range out {
		end := len(lines)
		if i+1 < len(out) {
			end = starts[i+1]
		}
		out[i].raw = strings.Join(lines[starts[i]:end], "\n") // with a block's lines (blank ones too) and comments under it
	}
	return out, body, nil
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

// values are a property's value as text: one, or a list's items.
func (p property) values() ([]string, bool) {
	n := p.node
	if n.Kind != yaml.SequenceNode {
		return []string{scalar(n)}, false
	}
	if len(n.Content) == 1 && n.Content[0].Kind == yaml.SequenceNode && len(n.Content[0].Content) == 1 && n.Style&yaml.FlowStyle != 0 {
		return []string{"[[" + scalar(n.Content[0].Content[0]) + "]]"}, false // [[link]] unquoted: a link, as Obsidian has it
	}
	items := []string{}
	for _, item := range n.Content {
		items = append(items, scalar(item))
	}
	return items, true
}

// text is a property's value as one text: a list's is none.
func (p property) text() string {
	if v, list := p.values(); !list {
		return v[0]
	}
	return ""
}

// scalar is a node's text: none for null, a list or a map.
func scalar(n *yaml.Node) string {
	if n.Kind == yaml.AliasNode {
		n = n.Alias
	}
	if n == nil || n.Kind != yaml.ScalarNode || n.ShortTag() == "!!null" {
		return ""
	}
	return n.Value
}
