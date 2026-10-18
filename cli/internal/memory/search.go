package memory

import (
	"crypto/sha256"
	"path/filepath"
	"strings"
	"sync"
	"unicode"
	"unicode/utf8"

	"github.com/blevesearch/bleve/v2"
	"github.com/blevesearch/bleve/v2/analysis/analyzer/custom"
	"github.com/blevesearch/bleve/v2/analysis/analyzer/keyword"
	"github.com/blevesearch/bleve/v2/analysis/lang/en"
	"github.com/blevesearch/bleve/v2/analysis/token/lowercase"
	unicodetok "github.com/blevesearch/bleve/v2/analysis/tokenizer/unicode"
	"github.com/blevesearch/bleve/v2/index/scorch"
	"github.com/blevesearch/bleve/v2/mapping"
	"github.com/blevesearch/bleve/v2/search"
	"github.com/blevesearch/bleve/v2/search/query"
	bleveindex "github.com/blevesearch/bleve_index_api"
)

// Hit is a note a search found, best first: the line that matched best
// (Line 0: only its title or properties did).
type Hit struct {
	Title string  `json:"title"`
	Path  string  `json:"path"`
	Type  string  `json:"type,omitempty"`
	Line  int     `json:"line,omitempty"`
	Text  string  `json:"text,omitempty"`
	Score float64 `json:"score"`
}

// Search finds the notes a query names, best first, one hit each. Words
// match in any form ("jumps" finds "jump"); "a phrase" matches as written;
// field:value looks in one field (type:character, title:mara, a property:
// faction:dorne); +word must be there, -word mustn't. A query that isn't
// one of those is searched as plain words.
func Search(root, text string) []Hit {
	out := []Hit{}
	if strings.TrimSpace(text) == "" {
		return out
	}
	idx, notes, err := indexOf(root)
	if err != nil {
		return out
	}
	req := bleve.NewSearchRequestOptions(queryOf(text), 50, 0, false)
	req.IncludeLocations = true
	res, err := idx.Search(req)
	if err != nil {
		req.Query = bleve.NewMatchQuery(text) // not a query: plain words
		if res, err = idx.Search(req); err != nil {
			return out
		}
	}
	for _, h := range res.Hits {
		n := notes[h.ID]
		hit := Hit{Title: n.Title, Path: n.Path, Type: n.Type, Score: h.Score}
		if line := bestLine(n.Text, h.Locations["text"], h.Locations["exact"]); line > 0 {
			hit.Line, hit.Text = line, strings.TrimSpace(strings.Split(n.Text, "\n")[line-1])
		}
		out = append(out, hit)
	}
	return out
}

// queryOf is a search's query: its standalone "phrases" matched as written
// (every word, in order, none stemmed), and the rest as a query string. A
// query that isn't one is all plain words.
func queryOf(text string) query.Query {
	if _, err := bleve.NewQueryStringQuery(text).Parse(); err != nil {
		return bleve.NewMatchQuery(text)
	}
	rest, phrases := phrasesOf(text)
	var parts []query.Query
	if strings.TrimSpace(rest) != "" {
		parts = append(parts, bleve.NewQueryStringQuery(rest))
	}
	for _, p := range phrases {
		inText, inTitle := bleve.NewMatchPhraseQuery(p), bleve.NewMatchPhraseQuery(p)
		inText.SetField("exact")
		inTitle.SetField("exact_title")
		inText.Analyzer, inTitle.Analyzer = "words", "words" // a field named apart from its property isn't found by its path
		parts = append(parts, bleve.NewDisjunctionQuery(inText, inTitle))
	}
	if len(parts) == 1 {
		return parts[0]
	}
	return bleve.NewConjunctionQuery(parts...)
}

// phrasesOf takes a query's standalone "phrases" out of it: not a field's
// name or value ("home region":x, title: "x"), +"x", -"x", "x"^2 or "x"~1,
// which stay in the query string. It reads the query as Bleve's lexer does:
// \ escapes the next character; a word ends at a space, :, ^ or ~.
func phrasesOf(text string) (string, []string) {
	type token struct {
		kind       byte // '"' a phrase, 'w' a word, else the operator
		start, end int
		phrase     string
	}
	var toks []token
	for i := 0; i < len(text); {
		r, size := utf8.DecodeRuneInString(text[i:])
		t := token{kind: text[i], start: i}
		switch {
		case unicode.IsSpace(r):
			i += size
			continue
		case t.kind == '"':
			var b strings.Builder
			for i++; i < len(text) && text[i] != '"'; i++ {
				if text[i] == '\\' && i+1 < len(text) {
					if i++; !strings.ContainsRune(reservedChars, rune(text[i])) {
						b.WriteByte('\\')
					}
				}
				b.WriteByte(text[i])
			}
			t.phrase = strings.TrimSpace(b.String())
			i++
		case strings.IndexByte("+-:<>=", t.kind) >= 0:
			i++
		default: // a word, ^boost or ~fuzziness
			if t.kind != '^' && t.kind != '~' {
				t.kind = 'w'
			}
			for i++; i < len(text) && (t.kind != 'w' || strings.IndexByte(":^~", text[i]) < 0) && text[i] != ' '; i++ {
				if text[i] == '\\' {
					i++
				}
			}
		}
		t.end = min(i, len(text))
		toks = append(toks, t)
	}
	var rest strings.Builder
	var phrases []string
	last := 0
	for k, t := range toks {
		if t.kind != '"' || k > 0 && strings.IndexByte("+-:<>=", toks[k-1].kind) >= 0 || k+1 < len(toks) && strings.IndexByte(":^~", toks[k+1].kind) >= 0 {
			continue
		}
		rest.WriteString(text[last:t.start] + " ")
		last = t.end
		if t.phrase != "" {
			phrases = append(phrases, t.phrase)
		}
	}
	rest.WriteString(text[last:])
	return rest.String(), phrases
}

// reservedChars are those \ escapes in a query (Bleve's); before another, \ is kept.
const reservedChars = "+-=&|><!(){}[]^\"~*?:\\/ "

// bestLine is the line (from 1) holding the most of a note's matched terms; 0: none.
func bestLine(text string, fields ...search.TermLocationMap) int {
	count := map[int]int{}
	for _, terms := range fields {
		for _, locations := range terms {
			for _, l := range locations {
				if int(l.Start) <= len(text) {
					count[strings.Count(text[:l.Start], "\n")+1]++
				}
			}
		}
	}
	best := 0
	for line, n := range count {
		if n > count[best] || n == count[best] && line < best {
			best = line
		}
	}
	return best
}

// indexes are the games' notes, indexed, as they were when last searched:
// remade when one changes.
var indexes = struct {
	sync.Mutex
	of map[string]indexed
}{of: map[string]indexed{}}

type indexed struct {
	sum   [32]byte
	idx   bleve.Index
	notes map[string]Note
}

func indexOf(root string) (bleve.Index, map[string]Note, error) {
	all := Notes(root)
	h := sha256.New()
	for _, n := range all {
		h.Write([]byte(n.Path + "\x00" + n.Text + "\x00"))
	}
	var sum [32]byte
	copy(sum[:], h.Sum(nil))
	key, _ := filepath.Abs(root)
	indexes.Lock()
	defer indexes.Unlock()
	if have, ok := indexes.of[key]; ok && have.sum == sum {
		return have.idx, have.notes, nil
	}
	props := map[string]bool{}
	for _, n := range all {
		for key := range n.Props {
			props[key] = true
		}
	}
	idx, err := bleve.NewUsing("", noteMapping(props), scorch.Name, bleve.Config.DefaultMemKVStore, nil) // in memory: Scorch (BM25 needs its field statistics), no path

	if err != nil {
		return nil, nil, err
	}
	notes := map[string]Note{}
	batch := idx.NewBatch()
	for _, n := range all {
		notes[n.Path] = n
		doc := map[string]any{"title": n.Title, "type": n.Type, "text": n.Text}
		for key, v := range n.Props {
			if !reserved(key) {
				doc[key] = v
			}
		}
		if err := batch.Index(n.Path, doc); err != nil {
			return nil, nil, err
		}
	}
	if err := idx.Batch(batch); err != nil {
		return nil, nil, err
	}
	if old, ok := indexes.of[key]; ok {
		old.idx.Close()
	}
	indexes.of[key] = indexed{sum, idx, notes}
	return idx, notes, nil
}

// reserved: a property of that name isn't searched by it: the note's own
// fields, the as-written ones, and Bleve's (_all...).
func reserved(key string) bool {
	switch key {
	case "title", "text", "type", "exact", "exact_title":
		return true
	}
	return strings.HasPrefix(key, "_")
}

// noteMapping: titles, text and properties in English (any form of a word
// finds it; a property is text, even one that looks like a date); titles and
// text also as written ("exact", for phrases); a note's type whole.
func noteMapping(props map[string]bool) mapping.IndexMapping {
	m := bleve.NewIndexMapping()
	m.AddCustomAnalyzer("words", map[string]any{"type": custom.Name, "tokenizer": unicodetok.Name, "token_filters": []string{lowercase.Name}})
	field := func(analyzer, name string, inAll bool) *mapping.FieldMapping {
		f := bleve.NewTextFieldMapping()
		f.Analyzer, f.Name, f.IncludeInAll = analyzer, name, inAll
		return f
	}
	doc := bleve.NewDocumentMapping()
	doc.Dynamic = false
	doc.AddFieldMappingsAt("title", field(en.AnalyzerName, "title", true), field("words", "exact_title", false))
	doc.AddFieldMappingsAt("text", field(en.AnalyzerName, "text", true), field("words", "exact", false))
	doc.AddFieldMappingsAt("type", field(keyword.Name, "type", true))
	for key := range props {
		if !reserved(key) {
			doc.AddFieldMappingsAt(key, field(en.AnalyzerName, key, true))
		}
	}
	m.DefaultMapping = doc
	m.DefaultAnalyzer = en.AnalyzerName
	m.ScoringModel = bleveindex.BM25Scoring
	return m
}
