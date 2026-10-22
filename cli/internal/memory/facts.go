package memory

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
	"github.com/Jumballaya/Journeyman-Engine/internal/realpath"
	"github.com/blevesearch/bleve/v2"
)

// Fact is a thing to keep in mind in every conversation: canon ("the
// Dornes were exiled from Varn"), a rule, the person's preference. A fact
// is a line: an observation with an Obsidian block id,
// "- [canon] The Dornes were exiled from Varn ^f3a9c".
type Fact struct {
	ID       string `json:"id"`
	Category string `json:"category"`
	Text     string `json:"text"`
	Scope    string `json:"scope"` // "game": the game's; "you": the person's, in every game
}

// Facts is where facts are kept: a game's design/Memory.md, or the
// person's own file (for every game).
type Facts struct {
	root, rel string // the game's folder and the file in it; root "": rel is the person's file
	scope     string
}

// GameFacts are a game's facts, in design/Memory.md.
func GameFacts(root string) Facts { return Facts{root, Dir + "/Memory.md", "game"} }

// PersonFacts are the person's own, in file, for every game.
func PersonFacts(file string) Facts { return Facts{"", file, "you"} }

func (f Facts) path() string {
	if f.root == "" {
		return f.rel
	}
	return filepath.Join(f.root, filepath.FromSlash(f.rel))
}

var factLine = regexp.MustCompile(`^- \[([^\]]+)\] (.+?) \^([a-z0-9-]+)\s*$`)

// All are the facts kept there, in order.
func (f Facts) All() []Fact {
	page, _ := f.read()
	var out []Fact
	lines := strings.Split(page, "\n")
	inCode := fenced(lines)
	for i, line := range lines {
		if m := factLine.FindStringSubmatch(line); m != nil && !inCode[i] {
			out = append(out, Fact{ID: m[3], Category: m[1], Text: m[2], Scope: f.scope})
		}
	}
	return out
}

// Remember keeps a fact (category "fact" when empty). replaces: the id of
// one it updates, which keeps its id. The same fact again is the one kept.
// similar: facts kept already that share most of its words: if one says the
// same, forget it, or remember again with replaces.
func (f Facts) Remember(category, text, replaces string) (fact Fact, similar []Fact, err error) {
	text = oneLine(text)
	if category = oneLine(strings.Trim(category, "[] ")); category == "" {
		category = "fact"
	}
	if text == "" {
		return Fact{}, nil, errors.New("say what to remember")
	}
	if strings.ContainsAny(category, "[]") || regexp.MustCompile(` \^[a-z0-9-]+$`).MatchString(text) {
		return Fact{}, nil, errors.New("a fact's category can't hold [ ], nor its text end in a ^block-id")
	}
	release, err := gamelock.Hold()
	if err != nil {
		return Fact{}, nil, err
	}
	defer release()
	page, err := f.read()
	if err != nil {
		return Fact{}, nil, err
	}
	all := f.All()
	for _, have := range all {
		// the same fact: the same text, a skip of onboarding's only another skip (it marks a step, not a thought)
		if have.ID != replaces && strings.EqualFold(have.Text, text) && strings.EqualFold(have.Category, "skip") == strings.EqualFold(category, "skip") {
			if replaces != "" { // the update says what another fact says: that one stays, the old one goes
				if _, err := f.forget(page, replaces); err != nil {
					return Fact{}, nil, err
				}
			}
			if category == "fact" || !strings.EqualFold(have.Category, "fact") || replaces != "" {
				return have, nil, nil // a category it has already stays (replaces changes it)
			}
			replaces = have.ID // a plain fact, asked under a category of its own: it's that now
			break
		}
	}
	if page == "" {
		page = f.empty()
	}
	if replaces != "" {
		lines := strings.Split(page, "\n")
		inCode := fenced(lines)
		for i, line := range lines {
			if m := factLine.FindStringSubmatch(line); m != nil && m[3] == replaces && !inCode[i] {
				fact = Fact{ID: replaces, Category: category, Text: text, Scope: f.scope}
				lines[i] = factText(fact)
				return fact, nil, f.save(strings.Join(lines, "\n"))
			}
		}
		return Fact{}, nil, fmt.Errorf("no fact %s to replace (recall lists them)", replaces)
	}
	fact = Fact{Category: category, Text: text, Scope: f.scope}
	for taken := true; taken; {
		id := make([]byte, 3)
		rand.Read(id)
		fact.ID, taken = "f"+hex.EncodeToString(id)[:5], false
		for _, have := range all {
			taken = taken || have.ID == fact.ID
		}
	}
	for _, have := range all {
		if alike(have.Text, text) {
			similar = append(similar, have)
		}
	}
	return fact, similar, f.save(addToSection(page, "Facts", factText(fact)))
}

// Forget removes a fact (by id).
func (f Facts) Forget(id string) (Fact, error) {
	release, err := gamelock.Hold()
	if err != nil {
		return Fact{}, err
	}
	defer release()
	page, err := f.read()
	if err != nil {
		return Fact{}, err
	}
	return f.forget(page, id)
}

func (f Facts) forget(page, id string) (Fact, error) {
	lines := strings.Split(page, "\n")
	inCode := fenced(lines)
	for i, line := range lines {
		if m := factLine.FindStringSubmatch(line); m != nil && m[3] == id && !inCode[i] {
			return Fact{ID: id, Category: m[1], Text: m[2], Scope: f.scope}, f.save(strings.Join(append(lines[:i], lines[i+1:]...), "\n"))
		}
	}
	return Fact{}, fmt.Errorf("no fact %s (recall lists them)", id)
}

// read is the page the facts are on ("" when there's none yet). A game's
// is in the game: not through a link, nor in a design/ that leads out.
func (f Facts) read() (string, error) {
	if f.root != "" {
		info, err := os.Lstat(f.path())
		if errors.Is(err, os.ErrNotExist) {
			return "", nil
		} else if err != nil {
			return "", err
		}
		if info.Mode()&os.ModeSymlink != 0 || !realpath.In(f.root, filepath.FromSlash(f.rel)) {
			return "", fmt.Errorf("%s leads out of the game: not read", f.rel)
		}
	}
	data, err := os.ReadFile(f.path())
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return "", err
	}
	return strings.ReplaceAll(string(data), "\r\n", "\n"), nil
}

func (f Facts) empty() string {
	if f.root == "" {
		return "# What I keep in mind\n\nThe person's facts and preferences for every game (`jm memory remember --you`).\n\n## Facts\n"
	}
	return page("Memory", "memory", "", "What to keep in mind in every conversation about the game: its canon, rules and the person's wishes (`remember`).\n",
		"\n## Facts\n\n## Relations\n- part_of [[Design]]\n")
}

func (f Facts) save(text string) error {
	if f.root != "" {
		return write(f.root, f.rel, text)
	}
	if err := os.MkdirAll(filepath.Dir(f.rel), 0o755); err != nil {
		return err
	}
	return atomicfile.WriteFile(f.rel, []byte(text), 0o644)
}

func factText(f Fact) string { return fmt.Sprintf("- [%s] %s ^%s", f.Category, f.Text, f.ID) }

// alike: two facts share most of their words (half or more of the shorter's).
func alike(a, b string) bool {
	wa, wb := words(a), words(b)
	if len(wa) > len(wb) {
		wa, wb = wb, wa
	}
	if len(wa) == 0 {
		return false
	}
	shared := 0
	for w := range wa {
		if wb[w] {
			shared++
		}
	}
	return shared*2 >= len(wa)
}

// words are a text's words that say something (no "the"), lower case, without a plural s.
func words(text string) map[string]bool {
	out := map[string]bool{}
	for _, w := range strings.FieldsFunc(strings.ToLower(text), func(r rune) bool { return !(r >= 'a' && r <= 'z' || r >= '0' && r <= '9' || r > 127) }) {
		if len(w) > 3 {
			w = strings.TrimSuffix(w, "s")
		}
		if !stopword[w] {
			out[w] = true
		}
	}
	return out
}

var stopword = map[string]bool{"a": true, "an": true, "the": true, "of": true, "to": true, "in": true, "on": true, "and": true, "or": true,
	"is": true, "are": true, "be": true, "it": true, "its": true, "for": true, "with": true, "at": true, "by": true, "that": true, "this": true}

// Recall are the facts a query is about, best first (all of them, in
// order, for no query): from each place facts are kept.
func Recall(query string, places ...Facts) []Fact {
	var all []Fact
	for _, p := range places {
		all = append(all, p.All()...)
	}
	if strings.TrimSpace(query) == "" || len(all) == 0 {
		return all
	}
	idx, err := bleve.NewMemOnly(noteMapping(nil))
	if err != nil {
		return all
	}
	defer idx.Close()
	for i, f := range all {
		idx.Index(fmt.Sprint(i), map[string]any{"text": f.Text + " " + f.Category, "type": f.Category})
	}
	req := bleve.NewSearchRequestOptions(queryOf(query), len(all), 0, false)
	res, err := idx.Search(req)
	if err != nil {
		return nil
	}
	out := []Fact{}
	for _, h := range res.Hits {
		var i int
		fmt.Sscan(h.ID, &i)
		out = append(out, all[i])
	}
	return out
}
