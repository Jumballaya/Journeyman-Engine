package memory

import (
	"fmt"
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

// relationOf reads a list item as a relation (false: it isn't one).
func relationOf(line string) (Relation, bool) {
	m := relationLine.FindStringSubmatch(line)
	if m == nil {
		return Relation{}, false
	}
	r := Relation{Type: m[1], Target: strings.TrimSpace(m[2])}
	for _, part := range attrParts(m[3]) {
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
// relation, or a property naming To), or mentioned in its text.
type Edge struct {
	From    string            `json:"from"`
	Type    string            `json:"type"` // "mentions": a [[link]] in its text
	To      string            `json:"to"`
	Attrs   map[string]string `json:"attrs,omitempty"`
	Inverse string            `json:"inverse,omitempty"` // the type as To has it, when the world knows it
}

// World is the game's notes and how they connect.
type World struct {
	Notes []Note `json:"notes"`
	Edges []Edge `json:"edges"`
	// Unwritten are titles the notes link that no note has yet.
	Unwritten []string `json:"unwritten"`
}

// WorldOf is the game's world as its notes say it.
func WorldOf(root string) World {
	w := World{Notes: Notes(root), Unwritten: []string{}}
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
	unwritten := map[string]string{} // by lower case: the spelling seen first
	edge := func(from Note, kind, link string, attrs map[string]string) {
		to := link
		if n, ok := w.find(link); ok {
			to = n.Title
		} else if seen, ok := unwritten[strings.ToLower(link)]; ok {
			to = seen
		} else {
			unwritten[strings.ToLower(link)] = link
			w.Unwritten = append(w.Unwritten, link)
		}
		w.Edges = append(w.Edges, Edge{From: from.Title, Type: kind, To: to, Attrs: attrs, Inverse: inverse[kind]})
	}
	for _, n := range w.Notes {
		stated := map[string]bool{}
		for _, key := range sortedKeys(n.Props) {
			values, _ := propValues(n.Props[key])
			for _, v := range values {
				for _, m := range wikilink.FindAllStringSubmatch(v, -1) {
					edge(n, key, strings.TrimSpace(m[1]), nil)
					stated[strings.ToLower(strings.TrimSpace(m[1]))] = true
				}
			}
		}
		for _, r := range n.Relations {
			edge(n, r.Type, r.Target, r.Attrs)
			stated[strings.ToLower(r.Target)] = true
		}
		for _, link := range n.Links {
			if !stated[strings.ToLower(link)] {
				edge(n, "mentions", link, nil)
			}
		}
	}
	return w
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

// node is the title a name has in the world: its note's, or a name the
// notes link that has no note yet (any case).
func (w World) node(name string) (Note, bool) {
	if n, ok := w.find(name); ok {
		return n, true
	}
	for _, u := range w.Unwritten {
		if strings.EqualFold(u, strings.TrimSpace(name)) {
			return Note{Title: u}, true
		}
	}
	return Note{}, false
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

// Around is what's within steps of a note (by title or file name; or a
// name linked that has no note yet, its Path empty): the
// notes reached, each with how many steps away, and the edges between them.
// Mentions are followed one step only: a note names much it isn't part of.
func (w World) Around(name string, steps int) (Note, map[string]int, []Edge, error) {
	center, ok := w.node(name)
	if !ok {
		return Note{}, nil, nil, fmt.Errorf("no note %q, and none links it", name)
	}
	dist := map[string]int{center.Title: 0}
	frontier := []string{center.Title}
	for step := 1; step <= steps && len(frontier) > 0; step++ {
		var next []string
		for _, e := range w.Edges {
			if e.Type == "mentions" && step > 1 {
				continue
			}
			for _, pair := range [][2]string{{e.From, e.To}, {e.To, e.From}} {
				if _, seen := dist[pair[1]]; !seen && slices.Contains(frontier, pair[0]) {
					dist[pair[1]] = step
					next = append(next, pair[1])
				}
			}
		}
		frontier = next
	}
	var edges []Edge
	for _, e := range w.Edges {
		_, from := dist[e.From]
		_, to := dist[e.To]
		if from && to && (e.Type != "mentions" || e.From == center.Title || e.To == center.Title) {
			edges = append(edges, e)
		}
	}
	return center, dist, edges, nil
}

// Path is the shortest chain of edges from one note to another, stated
// connections before mentions; nil: they don't connect.
func (w World) Path(from, to string) ([]Edge, error) {
	a, ok := w.node(from)
	if !ok {
		return nil, fmt.Errorf("no note %q, and none links it", from)
	}
	b, ok := w.node(to)
	if !ok {
		return nil, fmt.Errorf("no note %q, and none links it", to)
	}
	for _, withMentions := range []bool{false, true} {
		via := map[string]Edge{a.Title: {}}
		frontier := []string{a.Title}
		for len(frontier) > 0 {
			var next []string
			for _, e := range w.Edges {
				if e.Type == "mentions" && !withMentions {
					continue
				}
				for _, pair := range [][2]string{{e.From, e.To}, {e.To, e.From}} {
					if _, seen := via[pair[1]]; !seen && slices.Contains(frontier, pair[0]) {
						via[pair[1]] = e
						next = append(next, pair[1])
					}
				}
			}
			if _, reached := via[b.Title]; reached {
				var chain []Edge
				for at := b.Title; at != a.Title; {
					e := via[at]
					chain = append([]Edge{e}, chain...)
					if e.To == at {
						at = e.From
					} else {
						at = e.To
					}
				}
				return chain, nil
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
// and a place inside what's inside it.
func (w World) Conflicts() []Conflict {
	var out []Conflict
	pathOf := map[string]string{}
	for _, n := range w.Notes {
		pathOf[n.Title] = n.Path
	}
	type key struct{ a, b, kind string }
	seen := map[key]Edge{}
	for _, e := range w.Edges {
		if e.Type == "mentions" || e.Inverse == "" || len(e.Attrs) == 0 {
			continue
		}
		mine := key{e.From, e.To, e.Type}
		if other, ok := seen[key{e.To, e.From, e.Inverse}]; ok && !maps(other.Attrs, e.Attrs) {
			out = append(out, Conflict{pathOf[e.From], fmt.Sprintf("%s %s %s (%s), but %s says %s (%s)",
				e.From, e.Type, e.To, attrsText(e.Attrs), other.From, other.Type, attrsText(other.Attrs))})
		}
		seen[mine] = e
	}
	inside := map[string][]string{}
	for _, e := range w.Edges {
		switch {
		case e.Type == "located_in" || e.Type == "part_of":
			inside[e.From] = append(inside[e.From], e.To)
		case e.Type == "contains" || e.Type == "has_part":
			inside[e.To] = append(inside[e.To], e.From)
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
		for _, p := range ring {
			reported[p] = true
		}
		out = append(out, Conflict{pathOf[place], strings.Join(ring, ", ") + " are inside each other (located_in / part_of go round)"})
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

func maps(a, b map[string]string) bool {
	if len(a) != len(b) {
		return false
	}
	for k, v := range a {
		if !strings.EqualFold(strings.TrimSpace(b[k]), strings.TrimSpace(v)) {
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
	if r.Target == "" || strings.ContainsAny(r.Target, "[]|#^\n") { // a link may name a folder: Locations/Irongate
		return Note{}, fmt.Errorf("%q can't be linked: a note's title (or folder/title)", r.Target)
	}
	for k, v := range r.Attrs {
		if strings.ContainsAny(k, "():,\n") || strings.ContainsAny(v, "()\n") || strings.Contains(wikilink.ReplaceAllString(v, ""), ",") {
			return Note{}, fmt.Errorf("detail %s: %q can't hold ( ), a line break, or a comma outside a [[link]]", k, v)
		}
	}
	release, err := gamelock.Hold()
	if err != nil {
		return Note{}, err
	}
	defer release()
	n, ok := Find(root, from)
	if !ok {
		return Note{}, fmt.Errorf("no note %q", from)
	}
	for _, have := range n.Relations {
		if have.Type == r.Type && strings.EqualFold(have.Target, r.Target) {
			return Note{}, fmt.Errorf("%s already says %s", n.Title, relationText(have))
		}
	}
	text := addToSection(n.Text, "Relations", relationText(r))
	if err := write(root, n.Path, text); err != nil {
		return Note{}, err
	}
	return parse(n.Path, text), nil
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
