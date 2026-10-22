package memory

import (
	"cmp"
	"fmt"
	"maps"
	"regexp"
	"slices"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
)

// Relation is a typed link a note states, Basic Memory's way:
// "- road_to [[Irongate]] (distance: 120 km, via: [[Black Pass]])".
type Relation struct {
	Type   string            `json:"type"`
	Target string            `json:"target"`
	Attrs  map[string]string `json:"attrs,omitempty"`
}

var relationType = regexp.MustCompile(`^[\pL\pN_]+$`)

var relationLine = regexp.MustCompile(`^\s*[-*+] ([\pL\pN_]+) \[\[([^\]|#]+)(?:[#|][^\]]*)?\]\](?:\s*\((.*)\))?\s*$`)

// relationOf reads a list item as a relation (false: it isn't one): found
// in line, its code blanked; its details as raw has them (the same columns).
func relationOf(line, raw string) (Relation, bool) {
	m := relationLine.FindStringSubmatchIndex(line)
	if m == nil || strings.TrimSpace(line[m[4]:m[5]]) == "" { // a target in `code` isn't one
		return Relation{}, false
	}
	r, details := Relation{Type: line[m[2]:m[3]], Target: strings.TrimSpace(line[m[4]:m[5]])}, ""
	if m[6] >= 0 {
		details = raw[m[6]:m[7]]
	}
	for _, part := range attrParts(details) {
		if r.Attrs == nil {
			r.Attrs = map[string]string{}
		}
		if key, value, ok := strings.Cut(part, ":"); ok && strings.TrimSpace(key) != "" {
			r.Attrs[strings.TrimSpace(key)] = strings.TrimSpace(value)
		} else {
			r.Attrs["note"] = part
		}
	}
	return r, true
}

// attrParts are a relation's (details), split at the commas outside [[links]].
func attrParts(s string) []string {
	var out []string
	depth, start := 0, 0
	for i := 0; i <= len(s); i++ {
		switch {
		case i == len(s) || s[i] == ',' && depth == 0:
			if part := strings.TrimSpace(s[start:i]); part != "" {
				out = append(out, part)
			}
			start = i + 1
		case strings.HasPrefix(s[i:], "[["):
			depth++
		case strings.HasPrefix(s[i:], "]]") && depth > 0:
			depth--
		}
	}
	return out
}

// relationText is a relation as a list item.
func relationText(r Relation) string {
	text := fmt.Sprintf("- %s [[%s]]", r.Type, r.Target)
	if len(r.Attrs) > 0 {
		text += " (" + attrsText(r.Attrs) + ")"
	}
	return text
}

// attrsText is a relation's details: "key: value, ...", by key.
func attrsText(attrs map[string]string) string {
	keys := make([]string, 0, len(attrs))
	for k := range attrs {
		keys = append(keys, k)
	}
	slices.Sort(keys)
	var parts []string
	for _, k := range keys {
		parts = append(parts, k+": "+oneLine(attrs[k]))
	}
	return strings.Join(parts, ", ")
}

// Relation types the world knows how to read from the other side: each
// inverse pair once, and the ones that read the same both ways. A game adds
// or changes them in a note named Relations: "- [inverse] owns owned_by",
// "- [symmetric] trades_with".
var (
	inverses = [][2]string{
		{"located_in", "contains"}, {"part_of", "has_part"}, {"capital_of", "has_capital"},
		{"member_of", "has_member"}, {"leads", "led_by"}, {"rules", "ruled_by"}, {"owns", "owned_by"},
		{"parent_of", "child_of"}, {"branch_of", "has_branch"}, {"includes", "included_in"},
		{"employs", "works_for"}, {"created", "created_by"},
	}
	symmetric = []string{"road_to", "borders", "rival_of", "ally_of", "married_to", "sibling_of", "at_war_with", "trades_with", "related_to"}
)

// Edge is a connection between two notes: stated in From's note (a
// relation, or a property naming To), or mentioned in its text. From and To
// are names (World.Name); the paths say which notes, none for a name linked
// that has no note yet.
type Edge struct {
	From     string            `json:"from"`
	Type     string            `json:"type"` // "mentions": a [[link]] in its text
	To       string            `json:"to"`
	Attrs    map[string]string `json:"attrs,omitempty"`
	Inverse  string            `json:"inverse,omitempty"` // the type as To has it, when the world knows it
	FromPath string            `json:"fromPath"`
	ToPath   string            `json:"toPath,omitempty"`
}

// ends are the nodes an edge joins (id's).
func (e Edge) ends() (string, string) {
	return id(Note{Title: e.From, Path: e.FromPath}), id(Note{Title: e.To, Path: e.ToPath})
}

// id is a node's identity: its note's path, or a name with no note (any case).
func id(n Note) string {
	if n.Path != "" {
		return n.Path
	}
	return "\x00" + strings.ToLower(n.Title)
}

// World is the game's notes and how they connect.
type World struct {
	Notes []Note `json:"notes"`
	Edges []Edge `json:"edges"`
	// Unwritten are titles the notes link that no note has yet.
	Unwritten []string `json:"unwritten"`
	// Unread is the first note it couldn't read (left out): the world may lack what it says.
	Unread    error             `json:"-"`
	names     map[string]string // by path: the title, or folder/title when another note has it
	ambiguous []Conflict        // [[links]] to a title notes share
}

// Name is how the world calls a node: its title, or folder/title (as a
// [[link]] names it) when notes share the title.
func (w World) Name(n Note) string {
	if name, ok := w.names[n.Path]; ok {
		return name
	}
	return n.Title
}

// WorldOf is the game's world as its notes say it.
func WorldOf(root string) World {
	all, unread := notes(root)
	w := World{Notes: all, Unwritten: []string{}, Unread: unread, names: map[string]string{}}
	for _, n := range all {
		w.names[n.Path] = NameOf(all, n)
	}
	inverse := map[string]string{}
	pair := func(a, b string) { // a pairing replaces the ones a and b had
		for _, x := range []string{a, b} {
			if old, ok := inverse[x]; ok && inverse[old] == x {
				delete(inverse, old)
			}
		}
		inverse[a], inverse[b] = b, a
	}
	for _, s := range symmetric {
		pair(s, s)
	}
	for _, p := range inverses {
		pair(p[0], p[1])
	}
	for _, n := range w.Notes {
		if !strings.EqualFold(n.Title, "Relations") {
			continue
		}
		for _, o := range n.Observations {
			f := strings.Fields(o.Text)
			switch {
			case o.Category == "inverse" && len(f) == 2:
				pair(f[0], f[1])
			case o.Category == "symmetric" && len(f) == 1:
				pair(f[0], f[0])
			}
		}
	}
	edge := func(from Note, kind, link string, attrs map[string]string) string {
		if named := w.named(link); len(named) > 1 {
			_, err := w.node(link)
			w.ambiguous = append(w.ambiguous, Conflict{from.Path, fmt.Sprintf("%s's [[%s]]: %v; it's read as %s", w.Name(from), link, err, w.Name(named[0]))})
		}
		to, ok := w.find(link)
		if !ok {
			to = Note{Title: link}
			if seen := w.unwritten(link); seen != "" {
				to.Title = seen // the spelling seen first
			} else {
				w.Unwritten = append(w.Unwritten, link)
			}
		}
		w.Edges = append(w.Edges, Edge{From: w.Name(from), FromPath: from.Path, Type: kind, To: w.Name(to), ToPath: to.Path, Attrs: attrs, Inverse: inverse[kind]})
		return id(to)
	}
	for _, n := range w.Notes {
		stated := map[string]bool{}
		for _, key := range sortedKeys(n.Props) {
			values, _ := propValues(n.Props[key])
			for _, v := range values {
				for _, m := range wikilink.FindAllStringSubmatch(v, -1) {
					stated[edge(n, key, strings.TrimSpace(m[1]), nil)] = true
				}
			}
		}
		for _, r := range n.Relations {
			stated[edge(n, r.Type, r.Target, r.Attrs)] = true
		}
		for _, link := range n.Links {
			if !stated[w.target(link)] {
				edge(n, "mentions", link, nil)
			}
		}
	}
	return w
}

// unwritten is the spelling the world has for a name with no note ("": none).
func (w World) unwritten(name string) string {
	for _, u := range w.Unwritten {
		if strings.EqualFold(u, strings.TrimSpace(name)) {
			return u
		}
	}
	return ""
}

// target is the node a [[link]] names (its id).
func (w World) target(link string) string {
	if n, ok := w.find(link); ok {
		return id(n)
	}
	return id(Note{Title: strings.TrimSpace(link)})
}

func sortedKeys(m map[string]any) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	slices.Sort(keys)
	return keys
}

func propValues(v any) ([]string, bool) {
	switch v := v.(type) {
	case string:
		return []string{v}, false
	case []string:
		return v, true
	}
	return nil, false
}

// node is the note a name means (its title, file name, folder/title or
// path, any case), or a name the notes link that has no note yet (no Path).
func (w World) node(name string) (Note, error) {
	name = strings.TrimSpace(name)
	found := w.named(name)
	switch {
	case len(found) == 1:
		return found[0], nil
	case len(found) > 1:
		var names []string
		for _, n := range found {
			names = append(names, w.Name(n))
		}
		return Note{}, fmt.Errorf("%q is %d notes: say which (%s)", name, len(found), strings.Join(names, ", "))
	case w.unwritten(name) != "":
		return Note{Title: w.unwritten(name)}, nil
	case w.Unread != nil:
		return Note{}, fmt.Errorf("no note %q, and none links it; but a note can't be read (%v): it may be that one", name, w.Unread)
	}
	return Note{}, fmt.Errorf("no note %q, and none links it", name)
}

// named are the notes name may mean.
func (w World) named(name string) []Note { return named(w.Notes, name) }

// linksOnly says whether [[link]] names n and no other note.
func (w World) linksOnly(link string, n Note) bool {
	return linksTo(link, n) && !slices.ContainsFunc(w.Notes, func(o Note) bool { return o.Path != n.Path && linksTo(link, o) })
}

// at is the node an edge's end is.
func (w World) at(path, name string) Note {
	for _, n := range w.Notes {
		if path != "" && n.Path == path {
			return n
		}
	}
	return Note{Title: name}
}

// find is the note a [[link]] names.
func (w World) find(link string) (Note, bool) {
	for _, n := range w.Notes {
		if linksTo(link, n) {
			return n, true
		}
	}
	return Note{}, false
}

// Near is a node reached, steps away.
type Near struct {
	Note  Note
	Steps int
}

// Around is what's within steps of a note (by title, file name or
// folder/title; or a name linked that has no note yet, its Path empty): the
// note first, then the ones reached by steps, and the edges between them.
// Mentions are followed one step only: a note names much it isn't part of.
func (w World) Around(name string, steps int) ([]Near, []Edge, error) {
	center, err := w.node(name)
	if err != nil {
		return nil, nil, err
	}
	near := []Near{{center, 0}}
	dist := map[string]int{id(center): 0}
	frontier := []string{id(center)}
	for step := 1; step <= steps && len(frontier) > 0; step++ {
		var next []string
		for _, e := range w.Edges {
			if e.Type == "mentions" && step > 1 {
				continue
			}
			from, to := e.ends()
			for _, end := range []struct{ from, to, path, name string }{{from, to, e.ToPath, e.To}, {to, from, e.FromPath, e.From}} {
				if _, seen := dist[end.to]; !seen && slices.Contains(frontier, end.from) {
					dist[end.to] = step
					next = append(next, end.to)
					near = append(near, Near{w.at(end.path, end.name), step})
				}
			}
		}
		frontier = next
	}
	sort.SliceStable(near, func(i, j int) bool {
		return near[i].Steps < near[j].Steps || near[i].Steps == near[j].Steps && w.Name(near[i].Note) < w.Name(near[j].Note)
	})
	var edges []Edge
	for _, e := range w.Edges {
		from, to := e.ends()
		_, f := dist[from]
		_, t := dist[to]
		if f && t && (e.Type != "mentions" || from == id(center) || to == id(center)) {
			edges = append(edges, e)
		}
	}
	return near, edges, nil
}

// Path is the shortest chain of edges from one note to another, stated
// connections before mentions: empty from a note to itself, nil when they
// don't connect.
func (w World) Path(from, to string) ([]Edge, error) {
	a, err := w.node(from)
	if err != nil {
		return nil, err
	}
	b, err := w.node(to)
	if err != nil {
		return nil, err
	}
	start, goal := id(a), id(b)
	for _, withMentions := range []bool{false, true} {
		via := map[string]Edge{start: {}}
		frontier := []string{start}
		for len(frontier) > 0 {
			if _, reached := via[goal]; reached {
				chain := []Edge{}
				for at := goal; at != start; {
					e := via[at]
					chain = append([]Edge{e}, chain...)
					if f, t := e.ends(); t == at {
						at = f
					} else {
						at = t
					}
				}
				return chain, nil
			}
			var next []string
			for _, e := range w.Edges {
				if e.Type == "mentions" && !withMentions {
					continue
				}
				f, t := e.ends()
				for _, pair := range [][2]string{{f, t}, {t, f}} {
					if _, seen := via[pair[1]]; !seen && slices.Contains(frontier, pair[0]) {
						via[pair[1]] = e
						next = append(next, pair[1])
					}
				}
			}
			frontier = next
		}
	}
	return nil, nil
}

// Conflict is two things the world states that can't both be so.
type Conflict struct {
	Path    string `json:"path"`
	Message string `json:"message"`
}

// Conflicts are what the world states twice and differently: a two-way
// relation (or an inverse pair) stated from both ends with other details,
// a place inside what's inside it, and a [[link]] to a title notes share.
func (w World) Conflicts() []Conflict {
	out := slices.Clone(w.ambiguous)
	type key struct{ a, b, kind string }
	seen := map[key]Edge{}
	for _, e := range w.Edges {
		if e.Type == "mentions" || e.Inverse == "" || len(e.Attrs) == 0 {
			continue
		}
		from, to := e.ends()
		mine := key{from, to, e.Type}
		if other, ok := seen[key{to, from, e.Inverse}]; ok && !sameDetails(other.Attrs, e.Attrs) {
			out = append(out, Conflict{e.FromPath, fmt.Sprintf("%s %s %s (%s), but %s says %s (%s)",
				e.From, e.Type, e.To, attrsText(e.Attrs), other.From, other.Type, attrsText(other.Attrs))})
		}
		seen[mine] = e
	}
	inside, name, pathOf := map[string][]string{}, map[string]string{}, map[string]string{}
	for _, e := range w.Edges {
		from, to := e.ends()
		name[from], name[to], pathOf[from], pathOf[to] = e.From, e.To, e.FromPath, e.ToPath
		switch { // located_in and part_of, or what reads as one from the other side (contains, or the game's own)
		case e.Type == "located_in" || e.Type == "part_of":
			inside[from] = append(inside[from], to)
		case e.Inverse == "located_in" || e.Inverse == "part_of":
			inside[to] = append(inside[to], from)
		}
	}
	reported := map[string]bool{}
	for _, place := range sortedTitles(inside) {
		if reported[place] || !within(inside, place, place, map[string]bool{}) {
			continue
		}
		ring := []string{place} // the places that go round with it, said once
		for _, other := range sortedTitles(inside) {
			if other != place && within(inside, place, other, map[string]bool{}) && within(inside, other, place, map[string]bool{}) {
				ring = append(ring, other)
			}
		}
		var names []string
		path := ""
		for _, p := range ring {
			reported[p] = true
			names = append(names, name[p])
			path = cmp.Or(path, pathOf[p]) // the first with a note
		}
		out = append(out, Conflict{path, strings.Join(names, ", ") + " are inside each other (located_in / part_of go round)"})
	}
	return out
}

func within(inside map[string][]string, at, target string, seen map[string]bool) bool {
	for _, up := range inside[at] {
		if up == target {
			return true
		}
		if !seen[up] {
			seen[up] = true
			if within(inside, up, target, seen) {
				return true
			}
		}
	}
	return false
}

func sortedTitles(m map[string][]string) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

// sameDetails: the same keys, and values the same but for case and spaces.
func sameDetails(a, b map[string]string) bool {
	if len(a) != len(b) {
		return false
	}
	for k, v := range a {
		if w, ok := b[k]; !ok || !strings.EqualFold(strings.TrimSpace(w), strings.TrimSpace(v)) {
			return false
		}
	}
	return true
}

// Relate states a relation in a note (by title or file name), in its
// Relations section: "- type [[target]] (details)". The other side isn't
// written: the world reads it from this one.
func Relate(root, from string, r Relation) (Note, error) {
	r.Type, r.Target = strings.TrimSpace(r.Type), strings.TrimSpace(strings.TrimSuffix(strings.TrimPrefix(strings.TrimSpace(r.Target), "[["), "]]"))
	if !relationType.MatchString(r.Type) {
		return Note{}, fmt.Errorf("%q can't be a relation: one word, like located_in or road_to", r.Type)
	}
	if r.Target == "" || strings.ContainsAny(r.Target, "[]|#^`\n") { // a link may name a folder: Locations/Irongate
		return Note{}, fmt.Errorf("%q can't be linked: a note's title (or folder/title)", r.Target)
	}
	attrs := map[string]string{}
	for k, v := range r.Attrs {
		k, v = strings.TrimSpace(k), oneLine(v)
		if _, twice := attrs[k]; twice || k == "" {
			return Note{}, fmt.Errorf("detail %q: each needs a name of its own", k)
		}
		if strings.ContainsAny(k, "():,\n") || strings.ContainsAny(v, "()") || strings.Contains(wikilink.ReplaceAllString(v, ""), ",") {
			return Note{}, fmt.Errorf("detail %s: %q can't hold ( ), a line break, or a comma outside a [[link]]", k, v)
		}
		attrs[k] = v
	}
	r.Attrs = attrs
	release, err := gamelock.Hold()
	if err != nil {
		return Note{}, err
	}
	defer release()
	w := WorldOf(root)
	if w.Unread != nil {
		return Note{}, fmt.Errorf("a note can't be read (%v): it may be one meant, so nothing was written", w.Unread)
	}
	n, err := w.node(from)
	if err == nil && n.Path == "" {
		err = fmt.Errorf("no note %q", from)
	}
	if err != nil {
		return Note{}, err
	}
	switch named := w.named(r.Target); {
	case len(named) > 1:
		_, err := w.node(r.Target)
		return Note{}, err
	case len(named) == 1 && !w.linksOnly(r.Target, named[0]): // its path, say: written as a link that finds it alone
		if r.Target = NameOf(w.Notes, named[0]); !w.linksOnly(r.Target, named[0]) {
			return Note{}, fmt.Errorf("no [[link]] names %s alone (another note's path differs only in case): rename one", named[0].Path)
		}
	}
	for _, have := range n.Relations {
		if have.Type == r.Type && w.target(have.Target) == w.target(r.Target) {
			return Note{}, fmt.Errorf("%s already says %s", n.Title, relationText(have))
		}
	}
	text := addToSection(n.Text, "Relations", relationText(r))
	after := parse(n.Path, text)
	if !slices.ContainsFunc(after.Relations, func(have Relation) bool {
		return have.Type == r.Type && have.Target == r.Target && maps.Equal(have.Attrs, r.Attrs)
	}) {
		return Note{}, fmt.Errorf("%s wouldn't read back from %s (in a code block left open?): nothing was written", relationText(r), n.Path)
	}
	if err := write(root, n.Path, text); err != nil {
		return Note{}, err
	}
	return after, nil
}

// Summary is a note's first line of prose: what it is, in a line.
func Summary(n Note) string {
	_, body, _ := frontmatter(n.Text)
	lines := strings.Split(body, "\n")
	inCode := fenced(lines)
	for i, l := range lines {
		t := strings.TrimSpace(l)
		if t != "" && !inCode[i] && !strings.HasPrefix(t, "#") && !listItem.MatchString(l) {
			return t
		}
	}
	return ""
}
