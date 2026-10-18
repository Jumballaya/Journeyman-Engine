package memory

import (
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
	n := parse("design/x.md", "# X\n\n## Relations\n- road_to [[Irongate]] (distance: 120 km, via: [[Black Pass]], [[Ford, old]])\n- [feel] not a relation\n- see the [[Map]]\n```\n- road_to [[Code]]\n```\n")
	want := Relation{Type: "road_to", Target: "Irongate", Attrs: map[string]string{"distance": "120 km", "via": "[[Black Pass]]", "note": "[[Ford, old]]"}}
	if len(n.Relations) != 1 || n.Relations[0].Type != want.Type || n.Relations[0].Target != want.Target || !maps(n.Relations[0].Attrs, want.Attrs) {
		t.Errorf("relations: %+v", n.Relations)
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
	center, dist, edges, err := w.Around("ashvale", 2)
	if err != nil {
		t.Fatal(err)
	}
	if center.Title != "Ashvale" || dist["Irongate"] != 1 || dist["Varn"] != 2 || dist["House Dorne"] != 2 || dist["Mara Dorne"] != 2 {
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
	if _, err := Relate(root, "Irongate", Relation{Type: "road_to", Target: "duskhold"}); err == nil {
		t.Error("the same relation twice")
	}
	if _, err := Relate(root, "Irongate", Relation{Type: "road to", Target: "Duskhold"}); err == nil {
		t.Error("a relation of two words")
	}
	if _, err := Relate(root, "Irongate", Relation{Type: "trades_with", Target: "Duskhold", Attrs: map[string]string{"tonnes": "1200,"}}); err == nil {
		t.Error("a detail with a comma: it would read back as two")
	}
	if _, err := Relate(root, "Irongate", Relation{Type: "trades_with", Target: "Duskhold", Attrs: map[string]string{"via": "[[Ford, old]]"}}); err != nil {
		t.Error("a comma in a [[link]]:", err)
	}
	n, err := Relate(root, "Irongate", Relation{Type: "allied_by", Target: "Locations/Duskhold"})
	if err != nil || n.Relations[len(n.Relations)-1].Target != "Locations/Duskhold" {
		t.Errorf("a folder's note: %v %v", n.Relations, err)
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
}
