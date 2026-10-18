package memory

import (
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	"github.com/blevesearch/bleve/v2"
	"github.com/blevesearch/bleve/v2/search/query"
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

// Field names and values, quoted or spaced, stay fields; escapes are read as
// Bleve does; a query that isn't one is all plain words (R144-2).
func TestSearchKeepsFieldsAndEscapes(t *testing.T) {
	root := t.TempDir()
	NewDossier(root, "location", "Ashvale Keep", "A keep.", map[string]string{"home region": "Ashvale"})
	NewDossier(root, "location", "Dorne Hall", "Trades with Ashvale.", map[string]string{"home region": "Dorne"})
	NewDossier(root, "character", "Pirate", "Sails.", nil)
	NewDossier(root, "character", "Sailor", "Once a pirate, she has a title.", nil)
	NewDossier(root, "location", "Irongate", "Built on a cliff.", nil)
	for query, want := range map[string]string{
		`"home region":ashvale`:     "Ashvale Keep",
		`title: "pirate"`:           "Pirate",
		`title:"pirate"`:            "Pirate",
		`"built with the cliff`:     "Irongate",
		`"sails\." -title:sailor`:   "Pirate",
		`"once a \"pirate\"" title`: "Sailor",
	} {
		if got := strings.Join(sortedStrings(strings.Split(titles(Search(root, query)), ",")), ","); got != want {
			t.Errorf("%s: %s, want %s", query, got, want)
		}
	}
	if q, ok := queryOf(`"built with the cliff" +`).(*query.MatchQuery); !ok || q.Match != `"built with the cliff" +` || titles(Search(root, `"built with the cliff" +`)) != "Irongate" {
		t.Error("a query that isn't one isn't searched whole, as plain words")
	}

	jumps, jump := t.TempDir(), t.TempDir()
	NewDossier(jumps, "lore", "Jumps", "The cellar jumps.", nil)
	NewDossier(jump, "lore", "Jump", "The cellar jump.", nil)
	if titles(Search(jumps, `\"cellar\" "jump"`)) != "" || titles(Search(jump, `\"cellar\" "jump"`)) != "Jump" {
		t.Error(`\"cellar\" isn't a phrase; "jump" is`)
	}
	quoted := t.TempDir()
	NewDossier(quoted, "lore", "Sign", `The sign says "keep out" now.`, nil)
	if titles(Search(quoted, `"says \"keep out\" now"`)) != "Sign" {
		t.Error("an escaped quote inside a phrase")
	}
	if rest, phrases := phrasesOf(`"cliff\\" "jump" +"must" x:"y" "z"^2`); strings.Join(phrases, "|") != `cliff\|jump` || !strings.Contains(rest, `+"must" x:"y" "z"^2`) {
		t.Errorf("phrases after an escaped \\: %q %q", rest, phrases)
	}
}

// Ranked by BM25, over corpus statistics, kept in memory: nothing written
// (R144-1).
func TestSearchRanksByBM25InMemory(t *testing.T) {
	root := t.TempDir()
	t.Chdir(t.TempDir())
	NewDossier(root, "lore", "Often", "Ember ember ember, the ember forge.", nil)
	NewDossier(root, "lore", "Once", "An ember, and a long tale of many other things besides.", nil)
	NewDossier(root, "lore", "Never", "Ash.", nil)
	files := func() (out []string) {
		filepath.WalkDir(root, func(p string, _ os.DirEntry, _ error) error {
			out = append(out, p)
			return nil
		})
		cwd, _ := os.ReadDir(".")
		return append(out, strconv.Itoa(len(cwd)))
	}
	before := files()
	idx, _, err := indexOf(root)
	if err != nil {
		t.Fatal(err)
	}
	req := bleve.NewSearchRequest(queryOf("ember"))
	req.Explain = true
	res, err := idx.Search(req)
	if err != nil || len(res.Hits) != 2 {
		t.Fatalf("%v %v", res, err)
	}
	for i, h := range res.Hits {
		if e := h.Expl.String(); !strings.Contains(e, "as per bm25 model") || strings.Contains(e, "avgFieldLength=0.0") || math.IsInf(h.Score, 0) || h.Score <= 0 || i > 0 && h.Score > res.Hits[i-1].Score {
			t.Errorf("not BM25: %s %v", h.ID, h.Expl)
		}
	}
	if titles(Search(root, "ember")) != "Often,Once" {
		t.Errorf("ranking: %s", titles(Search(root, "ember")))
	}
	os.WriteFile(filepath.Join(root, Dir, "Lore", "Never.md"), []byte("---\ntype: lore\n---\n# Never\nAn ember now.\n"), 0o644)
	if hits := Search(root, "ember"); strings.Join(sortedStrings(strings.Split(titles(hits), ",")), ",") != "Never,Often,Once" {
		t.Errorf("after an edit: %+v", hits)
	}
	if after := files(); strings.Join(after, "\n") != strings.Join(before, "\n") {
		t.Errorf("index files written: %v", after)
	}
}

// A word escaped from its first character (\\ or \:) is a word: the phrase after it stands alone;
// an escaped space after a field's colon keeps what follows in the field's word.
func TestAnEscapedFirstCharacterIsAWord(t *testing.T) {
	for _, q := range []string{`\\ "jump"`, `\: "jump"`} {
		if rest, phrases := phrasesOf(q); len(phrases) != 1 || phrases[0] != "jump" || !strings.Contains(rest, q[:2]) {
			t.Errorf("%s: %q %v", q, rest, phrases)
		}
	}
	if rest, phrases := phrasesOf(`title:\ "jump"`); len(phrases) != 0 || rest != `title:\ "jump"` {
		t.Errorf("title:\\ \"jump\": %q %v", rest, phrases)
	}
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, Dir), 0o755)
	os.WriteFile(filepath.Join(root, Dir, "Moves.md"), []byte("# Moves\n\nThe hero jumps.\n"), 0o644)
	for _, q := range []string{`\\ "jump"`, `\: "jump"`} {
		if hits := Search(root, q); len(hits) != 0 {
			t.Errorf("%s matched jumps: %+v", q, hits)
		}
	}
}
