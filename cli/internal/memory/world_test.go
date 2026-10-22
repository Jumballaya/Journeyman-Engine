package memory

import (
	"maps"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// Two nations at war, a region between them, a capital each, and a family
// on both sides: the world as the notes say it.
func warringNations(t *testing.T) string {
	t.Helper()
	root := t.TempDir()
	for _, d := range []struct{ kind, name, summary string }{
		{"faction", "Varn", "A kingdom of the north, [[at war]] with [[Ost]]."},
		{"faction", "Ost", "The southern republic."},
		{"location", "Ashvale", "The contested valley between them."},
		{"location", "Irongate", "Varn's capital, on a cliff."},
		{"faction", "House Dorne", "Merchants with halls in both capitals."},
		{"character", "Mara Dorne", "Heir to House Dorne."},
	} {
		if _, err := NewDossier(root, d.kind, d.name, d.summary, nil); err != nil {
			t.Fatal(err)
		}
	}
	relate := func(from, kind, to string, attrs map[string]string) {
		t.Helper()
		if _, err := Relate(root, from, Relation{Type: kind, Target: to, Attrs: attrs}); err != nil {
			t.Fatal(err)
		}
	}
	relate("Varn", "at_war_with", "Ost", nil)
	relate("Irongate", "located_in", "Ashvale", nil)
	relate("Irongate", "capital_of", "Varn", nil)
	relate("House Dorne", "has_branch", "Irongate", nil)
	relate("Mara Dorne", "member_of", "House Dorne", nil)
	os.WriteFile(filepath.Join(root, Dir, "Characters", "Mara Dorne.md"), []byte(strings.Replace(
		mustRead(t, root, "design/Characters/Mara Dorne.md"), "home: \n", "home: \"[[Irongate]]\"\n", 1)), 0o644)
	return root
}

func mustRead(t *testing.T, root, rel string) string {
	t.Helper()
	data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(rel)))
	if err != nil {
		t.Fatal(err)
	}
	return string(data)
}

func TestRelationsAreReadWithTheirDetails(t *testing.T) {
	n := parse("design/x.md", "# X\n\n## Relations\n- road_to [[Irongate]] (distance: 120 km, via: [[Black Pass]], [[Ford, old]])\n- [feel] not a relation\n- see the [[Map]]\n"+
		"- ford_to [[Ford]] (route: use `ford`)\n- `road_to [[Span]]`\n- road_to `[[Spanned]]`\n```\n- road_to [[Code]]\n```\n\n    - road_to [[Indented]]\n")
	want := Relation{Type: "road_to", Target: "Irongate", Attrs: map[string]string{"distance": "120 km", "via": "[[Black Pass]]", "note": "[[Ford, old]]"}}
	if len(n.Relations) != 2 || n.Relations[0].Type != want.Type || n.Relations[0].Target != want.Target || !sameDetails(n.Relations[0].Attrs, want.Attrs) {
		t.Errorf("relations: %+v", n.Relations)
	}
	if n.Relations[1].Attrs["route"] != "use `ford`" || slices.ContainsFunc(n.Links, func(l string) bool { return strings.Contains(l, "pan") || l == "Code" || l == "Indented" }) {
		t.Errorf("code in a relation, and code's links: %+v %v", n.Relations[1], n.Links)
	}
	if relationText(want) != "- road_to [[Irongate]] (distance: 120 km, note: [[Ford, old]], via: [[Black Pass]])" {
		t.Errorf("as text: %s", relationText(want))
	}
}

// Making the second capital: what's around the region, how Mara reaches Ost,
// and what the notes link that isn't written yet.
func TestTheWorldAroundAPlace(t *testing.T) {
	root := warringNations(t)
	w := WorldOf(root)
	near, edges, err := w.Around("ashvale", 2)
	if err != nil {
		t.Fatal(err)
	}
	dist := steps(w, near)
	if near[0].Note.Title != "Ashvale" || dist["Irongate"] != 1 || dist["Varn"] != 2 || dist["House Dorne"] != 2 || dist["Mara Dorne"] != 2 {
		t.Errorf("around Ashvale: %v", dist)
	}
	if _, far := dist["Ost"]; far {
		t.Error("Ost is 3 steps from Ashvale")
	}
	var lines []string
	for _, e := range edges {
		lines = append(lines, e.From+" "+e.Type+" "+e.To+" / "+e.Inverse)
	}
	for _, want := range []string{"Irongate located_in Ashvale / contains", "Irongate capital_of Varn / has_capital",
		"House Dorne has_branch Irongate / branch_of", "Mara Dorne home Irongate / "} {
		if !slices.Contains(lines, want) {
			t.Errorf("no %q in %v", want, lines)
		}
	}
	path, err := w.Path("Mara Dorne", "Ost")
	if err != nil || len(path) != 3 || path[0].To != "Irongate" || path[2].Type != "at_war_with" {
		t.Errorf("Mara to Ost: %+v %v", path, err)
	}
	if !slices.Contains(w.Unwritten, "at war") {
		t.Errorf("unwritten: %v", w.Unwritten)
	}
	for _, same := range [][2]string{{"Irongate", "Irongate"}, {"Irongate", "Locations/Irongate"}, {"at war", "AT WAR"}} {
		if path, err := w.Path(same[0], same[1]); err != nil || path == nil || len(path) != 0 {
			t.Errorf("%s to %s: %+v %v", same[0], same[1], path, err)
		}
	}
}

func steps(w World, near []Near) map[string]int {
	out := map[string]int{}
	for _, n := range near {
		out[w.Name(n.Note)] = n.Steps
	}
	return out
}

// Two notes titled Harbor, one in each folder: two places, named by folder.
func TestNotesOfOneTitleAreTwoPlaces(t *testing.T) {
	root := t.TempDir()
	for path, text := range map[string]string{
		"North/Harbor.md": "# Harbor\n\nThe north harbor.\n\n## Relations\n- road_to [[Mountain]]\n",
		"South/Harbor.md": "# Harbor\n\nThe south harbor.\n\n## Relations\n- road_to [[Desert]]\n",
	} {
		os.MkdirAll(filepath.Join(root, Dir, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(root, Dir, path), []byte(text), 0o644)
	}
	w := WorldOf(root)
	for _, c := range [][3]string{{"North/Harbor", "The north harbor.", "Mountain"}, {"south/harbor.md", "The south harbor.", "Desert"}} {
		near, edges, err := w.Around(c[0], 1)
		if err != nil || len(near) != 2 || len(edges) != 1 || Summary(near[0].Note) != c[1] || w.Name(near[1].Note) != c[2] {
			t.Errorf("around %s: %+v %+v %v", c[0], near, edges, err)
		}
	}
	if _, _, err := w.Around("Harbor", 1); err == nil || !strings.Contains(err.Error(), "North/Harbor, South/Harbor") {
		t.Errorf("Harbor is two notes: %v", err)
	}
	if path, err := w.Path("North/Harbor", "South/Harbor"); path != nil || err != nil {
		t.Errorf("the harbors don't connect: %+v %v", path, err)
	}
	if _, err := Relate(root, "North/Harbor", Relation{Type: "ferry_to", Target: "harbor"}); err == nil {
		t.Error("a ferry to either harbor")
	}
	Relate(root, "North/Harbor", Relation{Type: "ferry_to", Target: "South/Harbor"})
	path, _ := WorldOf(root).Path("North/Harbor", "South/Harbor")
	if len(path) != 1 || path[0].From != "North/Harbor" || path[0].ToPath != "design/South/Harbor.md" {
		t.Errorf("the ferry: %+v", path)
	}
	Relate(root, "South/Harbor", Relation{Type: "located_in", Target: "Desert"})
	os.WriteFile(filepath.Join(root, Dir, "Desert.md"), []byte("# Desert\n\n## Relations\n- located_in [[South/Harbor]]\n"), 0o644)
	c := WorldOf(root).Conflicts()
	if len(c) != 1 || c[0].Path != "design/Desert.md" || c[0].Message != "Desert, South/Harbor are inside each other (located_in / part_of go round)" {
		t.Errorf("the desert and its harbor: %+v", c)
	}
	os.WriteFile(filepath.Join(root, Dir, "Map.md"), []byte("# Map\n\nSee [[Harbor]].\n"), 0o644)
	if c := WorldOf(root).Conflicts(); len(c) != 2 || c[0].Path != "design/Map.md" || !strings.Contains(c[0].Message, "Map's [[Harbor]]") {
		t.Errorf("a link to either harbor: %+v", c)
	}
	if n, err := Relate(root, "Map", Relation{Type: "shows", Target: "design/Desert.md"}); err != nil || n.Relations[0].Target != "Desert" {
		t.Errorf("a note by its path: %+v %v", n.Relations, err)
	}
}

// A road stated from both ends with two lengths, and a place inside itself.
func TestTheWorldsConflicts(t *testing.T) {
	root := warringNations(t)
	if c := WorldOf(root).Conflicts(); len(c) != 0 {
		t.Fatalf("a consistent world: %v", c)
	}
	NewDossier(root, "location", "Duskhold", "Ost's capital.", nil)
	Relate(root, "Irongate", Relation{Type: "road_to", Target: "Duskhold", Attrs: map[string]string{"distance": "120 km"}})
	Relate(root, "Duskhold", Relation{Type: "road_to", Target: "Irongate", Attrs: map[string]string{"distance": "90 km"}})
	Relate(root, "Ashvale", Relation{Type: "located_in", Target: "Irongate"})
	var got []string
	for _, c := range WorldOf(root).Conflicts() {
		got = append(got, c.Path+": "+c.Message)
	}
	all := strings.Join(got, "\n")
	if len(got) != 2 || !strings.Contains(all, "road_to") || !strings.Contains(all, "120 km") || !strings.Contains(all, "90 km") ||
		!strings.Contains(all, "Ashvale, Irongate are inside each other") {
		t.Errorf("conflicts:\n%s", strings.Join(got, "\n"))
	}
	before := mustRead(t, root, "design/Locations/Irongate.md")
	for _, again := range []string{"duskhold", "Locations/Duskhold", "locations/duskhold.md"} {
		if _, err := Relate(root, "Irongate", Relation{Type: "road_to", Target: again, Attrs: map[string]string{"distance": "1 km"}}); err == nil {
			t.Errorf("the same relation twice, to %s", again)
		}
	}
	Relate(root, "Irongate", Relation{Type: "rival_of", Target: "Nowhere"})
	if _, err := Relate(root, "Irongate", Relation{Type: "rival_of", Target: "NOWHERE"}); err == nil {
		t.Error("the same relation to an unwritten name twice")
	}
	if _, err := Relate(root, "Irongate", Relation{Type: "trades_with", Target: "nowhere"}); err != nil {
		t.Error("another relation to the same name:", err)
	}
	os.MkdirAll(filepath.Join(root, Dir, "Old"), 0o755)
	os.WriteFile(filepath.Join(root, Dir, "Old", "Duskhold.md"), []byte("# Duskhold\n"), 0o644)
	if _, err := Relate(root, "Irongate", Relation{Type: "road_to", Target: "Old/Duskhold"}); err != nil {
		t.Error("the other Duskhold:", err)
	}
	if after := mustRead(t, root, "design/Locations/Irongate.md"); strings.Count(after, "road_to") != strings.Count(before, "road_to")+1 {
		t.Errorf("Irongate:\n%s", after)
	}
	if _, err := Relate(root, "Irongate", Relation{Type: "road to", Target: "Duskhold"}); err == nil {
		t.Error("a relation of two words")
	}
	if _, err := Relate(root, "Irongate", Relation{Type: "trades_with", Target: "Locations/Duskhold", Attrs: map[string]string{"tonnes": "1200,"}}); err == nil {
		t.Error("a detail with a comma: it would read back as two")
	}
	if _, err := Relate(root, "Irongate", Relation{Type: "trades_with", Target: "Locations/Duskhold", Attrs: map[string]string{"via": "[[Ford, old]]"}}); err != nil {
		t.Error("a comma in a [[link]]:", err)
	}
	n, err := Relate(root, "Irongate", Relation{Type: "allied_by", Target: "Locations/Duskhold"})
	if err != nil || n.Relations[len(n.Relations)-1].Target != "Locations/Duskhold" {
		t.Errorf("a folder's note: %v %v", n.Relations, err)
	}
}

// What Relate says reads back as said, or nothing is written.
func TestRelationsReadBackAsStated(t *testing.T) {
	root := t.TempDir()
	NewDossier(root, "location", "Fort", "", nil)
	details := map[string]string{"route": "use `ford`", "distance": "3 km", "größe der furt": "", "via": "[[Ford, old]]"}
	n, err := Relate(root, "Fort", Relation{Type: "road_to", Target: "Vale", Attrs: details})
	if err != nil || len(n.Relations) == 0 || !maps.Equal(n.Relations[len(n.Relations)-1].Attrs, details) {
		t.Fatalf("details: %+v %v", n.Relations, err)
	}
	if e := WorldOf(root).Edges; !slices.ContainsFunc(e, func(e Edge) bool { return e.To == "Vale" && e.Attrs["route"] == "use `ford`" }) || !slices.Contains(n.Links, "Ford, old") {
		t.Errorf("edges: %+v links: %v", e, n.Links)
	}
	before := mustRead(t, root, "design/Locations/Fort.md")
	for _, bad := range []Relation{
		{Type: "near", Target: "A", Attrs: map[string]string{"": "x"}},
		{Type: "near", Target: "A", Attrs: map[string]string{"  ": "x"}},
		{Type: "near", Target: "A", Attrs: map[string]string{"a": "x", " a": "y"}},
		{Type: "near", Target: "`A`"},
	} {
		if _, err := Relate(root, "Fort", bad); err == nil {
			t.Errorf("%+v was stated", bad)
		}
	}
	if mustRead(t, root, "design/Locations/Fort.md") != before {
		t.Error("a refused relation was written")
	}
	for name, text := range map[string]string{"Open": "# Open\n\n## Relations\n```\n- example\n", "Unheaded": "# Unheaded\n\n```\ncode\n"} {
		os.WriteFile(filepath.Join(root, Dir, name+".md"), []byte(text), 0o644)
		if _, err := Relate(root, name, Relation{Type: "near", Target: "Fort"}); err == nil || mustRead(t, root, "design/"+name+".md") != text {
			t.Errorf("%s: a relation in an open code block: %v", name, err)
		}
	}
}

// A game's own pairing replaces the built-in one; an unwritten name is one
// node whatever its case.
func TestTheGamesRelationsAndUnwrittenNames(t *testing.T) {
	root := t.TempDir()
	Observe(root, "Relations", "inverse", "located_in houses")
	NewDossier(root, "location", "Fort", "", nil)
	NewDossier(root, "location", "Vale", "Near [[ost]].", nil)
	Relate(root, "Fort", Relation{Type: "located_in", Target: "Vale"})
	Relate(root, "Fort", Relation{Type: "rival_of", Target: "Ost"})
	w := WorldOf(root)
	for _, e := range w.Edges {
		if e.Type == "located_in" && e.Inverse != "houses" {
			t.Errorf("located_in reads as %q from the other side", e.Inverse)
		}
	}
	if len(w.Unwritten) != 1 {
		t.Errorf("unwritten: %v", w.Unwritten)
	}
	if path, _ := w.Path("Vale", "ost"); len(path) != 2 || path[1].To != "Ost" { // stated ties before Vale's mention
		t.Errorf("Vale to Ost: %+v", path)
	}
	if c := w.Conflicts(); len(c) != 0 {
		t.Errorf("Fort is in Vale, and nothing in it: %v", c)
	}
	Relate(root, "Fort", Relation{Type: "contains", Target: "Vale"}) // contains is no longer located_in's other side
	if c := WorldOf(root).Conflicts(); len(c) != 0 {
		t.Errorf("contains: %v", c)
	}
	Relate(root, "Fort", Relation{Type: "houses", Target: "Vale"})
	if c := WorldOf(root).Conflicts(); len(c) != 1 || c[0].Message != "Fort, Vale are inside each other (located_in / part_of go round)" || c[0].Path != "design/Locations/Fort.md" {
		t.Errorf("Fort houses what it's in: %v", c)
	}
}

// A part that has its whole as a part.
func TestPartsGoRound(t *testing.T) {
	root := t.TempDir()
	NewDossier(root, "item", "Hilt", "", nil)
	Relate(root, "Hilt", Relation{Type: "part_of", Target: "Sword"})
	Relate(root, "Hilt", Relation{Type: "has_part", Target: "Sword"})
	if c := WorldOf(root).Conflicts(); len(c) != 1 || !strings.Contains(c[0].Message, "Sword, Hilt") || c[0].Path != "design/Items/Hilt.md" {
		t.Errorf("parts: %v", c)
	}
}

// Details differ when the keys do, though both values are empty.
func TestDetailsDifferByTheirKeys(t *testing.T) {
	for _, c := range []struct {
		a, b map[string]string
		same bool
	}{
		{map[string]string{"distance": ""}, map[string]string{"via": ""}, false},
		{map[string]string{"distance": ""}, map[string]string{"distance": ""}, true},
		{map[string]string{"distance": "120 KM "}, map[string]string{"distance": "120 km"}, true},
		{map[string]string{"distance": "120 km"}, map[string]string{"distance": "90 km"}, false},
	} {
		if sameDetails(c.a, c.b) != c.same {
			t.Errorf("%v and %v: same %v", c.a, c.b, !c.same)
		}
	}
}

// A note it can't read is said, not taken for none.
func TestTheWorldSaysWhatItCouldntRead(t *testing.T) {
	root := t.TempDir()
	if w := WorldOf(root); w.Unread != nil {
		t.Errorf("no design/: %v", w.Unread)
	}
	NewDossier(root, "location", "Fort", "", nil)
	fort := filepath.Join(root, Dir, "Locations", "Fort.md")
	os.Chmod(fort, 0)
	defer os.Chmod(fort, 0o644)
	if w := WorldOf(root); w.Unread == nil || !strings.Contains(w.Unread.Error(), "Fort.md") {
		t.Errorf("an unreadable note: %v", w.Unread)
	}
	if _, _, err := WorldOf(root).Around("Fort", 1); err == nil || !strings.Contains(err.Error(), "can't be read") {
		t.Errorf("Fort, unread: %v", err)
	}
	NewDossier(root, "location", "Vale", "", nil)
	if _, err := Relate(root, "Vale", Relation{Type: "near", Target: "Fort"}); err == nil {
		t.Error("a relation written beside a note it couldn't read")
	}
	os.Chmod(fort, 0o644)
	locations := filepath.Dir(fort)
	os.Chmod(locations, 0)
	defer os.Chmod(locations, 0o755)
	if w := WorldOf(root); w.Unread == nil || !strings.Contains(w.Unread.Error(), "Locations") {
		t.Errorf("an unlistable folder: %v", w.Unread)
	}
}

// A relation is written as a link that finds its note alone; when none can
// (two paths differing only in case), it isn't written.
func TestARelationsLinkFindsItsNoteAlone(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	for _, d := range []string{"North", "South"} {
		os.MkdirAll(filepath.Join(root, Dir, d), 0o755)
		os.WriteFile(filepath.Join(root, Dir, d, "Harbor.md"), []byte("---\ntitle: \"Harbor\"\n---\n\n# Harbor\n"), 0o644)
	}
	n, err := Relate(root, "Pillars", Relation{Type: "mentions_place", Target: "design/South/Harbor.md"})
	if err != nil || !slices.ContainsFunc(n.Relations, func(r Relation) bool { return r.Target == "South/Harbor" }) {
		t.Errorf("%v %+v", err, n.Relations)
	}
	w := World{Notes: []Note{{Title: "Harbor", Path: "design/North/Harbor.md"}, {Title: "Harbor", Path: "design/north/Harbor.md"}}}
	if w.linksOnly("north/Harbor", w.Notes[1]) || NameOf(w.Notes, w.Notes[1]) != "design/north/Harbor.md" {
		t.Error("a link for one of two paths differing in case")
	}
}

// A title a [[link]] can't hold (C#: the link ends at #; `C`: a code span)
// isn't the name written: the note's file name is.
func TestARelationToATitleALinkCantHoldUsesItsFileName(t *testing.T) {
	for file, title := range map[string]string{"csharp": "C#", "c": "`C`"} {
		root := t.TempDir()
		Scaffold(root)
		os.WriteFile(filepath.Join(root, Dir, file+".md"), []byte("---\ntitle: \""+title+"\"\n---\n\n# "+title+"\n"), 0o644)
		n, err := Relate(root, "Pillars", Relation{Type: "uses", Target: "design/" + file + ".md"})
		if err != nil || !slices.ContainsFunc(n.Relations, func(r Relation) bool { return r.Target == file }) {
			t.Errorf("%s: %v %+v", title, err, n.Relations)
		}
	}
}

// A link resolves by link rules, not a tool's: [[design/Harbor]] is
// design/design/Harbor.md's (a note's path is past design/), so it's written.
func TestARelationsLinkIsByLinkRules(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	os.MkdirAll(filepath.Join(root, Dir, "design"), 0o755)
	for _, p := range []string{"Harbor.md", "design/Harbor.md"} {
		os.WriteFile(filepath.Join(root, Dir, p), []byte("---\ntitle: \"Harbor\"\n---\n\n# Harbor\n"), 0o644)
	}
	n, err := Relate(root, "Pillars", Relation{Type: "mentions_place", Target: "design/design/Harbor.md"})
	if err != nil || !slices.ContainsFunc(n.Relations, func(r Relation) bool { return r.Target == "design/Harbor" }) {
		t.Errorf("%v %+v", err, n.Relations)
	}
}
