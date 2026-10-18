package memory

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func titles(hits []Hit) string {
	var out []string
	for _, h := range hits {
		out = append(out, h.Title)
	}
	return strings.Join(out, ",")
}

// Words in any form, fields, properties, phrases, must and mustn't; the best
// line of each note; an edit seen by the next search.
func TestSearchRanksTheNotesAQueryNames(t *testing.T) {
	root := t.TempDir()
	NewDossier(root, "character", "Mara Dorne", "Heir of House Dorne. She jumps rooftops in [[Irongate]].", map[string]string{"faction": "[[House Dorne]]"})
	NewDossier(root, "character", "Kael", "A smith who never leaves the forge.", map[string]string{"faction": "[[Iron Watch]]"})
	NewDossier(root, "location", "Irongate", "Capital of [[Varn]], built on a cliff.", nil)
	Observe(root, "Levels", "feel", "the rooftop jump in 1-2 feels floaty")
	NewDossier(root, "lore", "Iron Charter", "Signed in a cellar.", map[string]string{"era": "2026-10-18", "_all": "x", "exact": "jumping"})

	if hits := Search(root, "jumping"); strings.Join(sortedStrings(strings.Split(titles(hits), ",")), ",") != "Iron Charter,Levels,Mara Dorne,Pillars" {
		t.Errorf("jumping (the Pillars' example says jumps): %s", titles(hits))
	}
	for query, want := range map[string]string{
		"type:character":         "Kael,Mara Dorne",
		"faction:dorne":          "Mara Dorne",
		`"built on a cliff"`:     "Irongate",
		"+type:character -forge": "Mara Dorne",
		"title:kael":             "Kael",
		"capital (of [[varn":     "Irongate", // not a query: plain words
		`"built with the cliff"`: "",
		`"of varn" capital`:      "Irongate",
		`era:"2026-10-18"`:       "Iron Charter",
		"_all:charter":           "Iron Charter",
		`"cellar \"yes\" now"`:   "",
		`"jump"`:                 "Levels", // as written: not the Charter's jumping
	} {
		got := strings.Split(titles(Search(root, query)), ",")
		if strings.Join(sortedStrings(got), ",") != want {
			t.Errorf("%s: %v", query, got)
		}
	}
	hit := Search(root, "floaty rooftop")[0]
	if hit.Title != "Levels" || !strings.Contains(hit.Text, "floaty") || hit.Line == 0 || hit.Type != "note" {
		t.Errorf("best line: %+v", hit)
	}
	os.WriteFile(filepath.Join(root, Dir, "Characters", "Kael.md"), []byte("---\ntype: character\n---\n# Kael\nNow a pirate.\n"), 0o644)
	if titles(Search(root, "pirate")) != "Kael" {
		t.Error("an edit isn't searched")
	}
	if len(Search(root, "  ")) != 0 {
		t.Error("nothing searched for found something")
	}
}

func sortedStrings(s []string) []string {
	out := append([]string(nil), s...)
	for i := range out {
		for j := i + 1; j < len(out); j++ {
			if out[j] < out[i] {
				out[i], out[j] = out[j], out[i]
			}
		}
	}
	return out
}
