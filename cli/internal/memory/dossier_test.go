package memory

import (
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
)

// The first character writes its template (to edit) and Characters.base;
// the dossier has the props given, the template's others empty, and its summary.
func TestADossierIsItsKindsTemplateFilledIn(t *testing.T) {
	root := t.TempDir()
	n, err := NewDossier(root, "character", "Mara Dorne", "Heir of House Dorne, raised in [[Varn]].",
		map[string]string{"faction": "[[House Dorne]]", "home": "[[Irongate]]", "age": "34"})
	if err != nil {
		t.Fatal(err)
	}
	if n.Path != "design/Characters/Mara Dorne.md" || n.Type != "character" || n.Title != "Mara Dorne" {
		t.Errorf("dossier: %+v", n)
	}
	if n.Props["faction"] != "[[House Dorne]]" || n.Props["role"] != "" || n.Props["age"] != "34" ||
		!slices.Equal(n.Props["tags"].([]string), []string{"character"}) {
		t.Errorf("props: %v", n.Props)
	}
	if !slices.Equal(n.Links, []string{"House Dorne", "Irongate", "Varn"}) {
		t.Errorf("links: %v", n.Links)
	}
	if !strings.Contains(n.Text, "# Mara Dorne\n\nHeir of House Dorne") || !strings.Contains(n.Text, "## Personality") {
		t.Errorf("text:\n%s", n.Text)
	}
	base, _ := os.ReadFile(filepath.Join(root, Dir, "Characters.base"))
	if !strings.Contains(string(base), `- 'note.type == "character"'`) || !strings.Contains(string(base), `- '!file.inFolder("_templates")'`) || !strings.Contains(string(base), "- note.faction") {
		t.Errorf("base:\n%s", base)
	}
	if _, err := os.Stat(filepath.Join(root, Dir, Templates, "Characters.md")); err != nil {
		t.Error("the template isn't the game's to edit:", err)
	}
	for _, note := range Notes(root) {
		if strings.Contains(note.Path, Templates) {
			t.Errorf("a template is a note: %s", note.Path)
		}
	}
	if _, err := NewDossier(root, "Characters", "mara dorne", "", nil); err == nil || !strings.Contains(err.Error(), "already exists") {
		t.Errorf("a second Mara: %v", err)
	}
}

// A template of the game's own is a kind; one of a built-in kind's folder replaces it.
func TestTheGamesTemplatesAreItsKinds(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, Dir, Templates), 0o755)
	os.WriteFile(filepath.Join(root, Dir, Templates, "Vehicles.md"), []byte("---\ntype: vehicle\nspeed: 10\ncrew:\n  - pilot\n---\n\n# {{title}}\n\n{{summary}}\n"), 0o644)
	os.WriteFile(filepath.Join(root, Dir, Templates, "Items.md"), []byte("---\ntype: item\nweight: \n---\n# {{title}}\n"), 0o644)
	var vehicle, item Kind
	for _, k := range Kinds(root) {
		switch k.Name {
		case "vehicle":
			vehicle = k
		case "item":
			item = k
		}
	}
	if vehicle.Folder != "Vehicles" || !slices.Equal(vehicle.Props, []string{"speed", "crew"}) || !slices.Equal(item.Props, []string{"weight"}) {
		t.Errorf("kinds: %+v %+v", vehicle, item)
	}
	n, err := NewDossier(root, "vehicle", "Iron Barge", "Hauls ore down the [[Black River]].", map[string]string{"speed": "4"})
	if err != nil {
		t.Fatal(err)
	}
	if n.Props["speed"] != "4" || !slices.Equal(n.Props["crew"].([]string), []string{"pilot"}) {
		t.Errorf("props: %v", n.Props)
	}
	if _, err := NewDossier(root, "spaceship", "Nope", "", nil); err == nil || !strings.Contains(err.Error(), "vehicle") {
		t.Errorf("an unknown kind: %v", err)
	}
}

// What reviews found: a list's quoted comma and a comment, a kind with an
// apostrophe, a property name YAML can't hold, a design note's name, and a
// folder that's a link (not listed, so not found).
func TestDossiersHoldUpToTheOddCase(t *testing.T) {
	root := t.TempDir()
	note := parse("design/x.md", "---\nallies: [\"Dorne, Mara\", 'it''s', \"[[Varn]]\"]\ncrew: # who\n  - pilot\n  - \"[[Gunner]]\"\n---\n")
	if !slices.Equal(note.Props["allies"].([]string), []string{"Dorne, Mara", "it's", "[[Varn]]"}) ||
		!slices.Equal(note.Props["crew"].([]string), []string{"pilot", "[[Gunner]]"}) || !slices.Equal(note.Links, []string{"Varn", "Gunner"}) {
		t.Errorf("props %v, links %v", note.Props, note.Links)
	}
	os.MkdirAll(filepath.Join(root, Dir, Templates), 0o755)
	os.WriteFile(filepath.Join(root, Dir, Templates, "Logs.md"), []byte("---\ntype: captain's log\n---\n# {{title}}\n"), 0o644)
	if _, err := NewDossier(root, "captain's log", "Day 1", "", nil); err != nil {
		t.Fatal(err)
	}
	if base, _ := os.ReadFile(filepath.Join(root, Dir, "Logs.base")); !strings.Contains(string(base), `- 'note.type == "captain''s log"'`) {
		t.Errorf("base:\n%s", base)
	}
	if _, err := NewDossier(root, "item", "Sword", "", map[string]string{"- bad": "x"}); err == nil {
		t.Error("a property named - bad")
	}
	if _, err := NewDossier(root, "item", "Sword", "", map[string]string{"type": "weapon"}); err == nil {
		t.Error("a prop changed the dossier's type: its base wouldn't show it")
	}
	guard := parse("design/y.md", "---\nmembers: [King's Guard, \"[[Varn]]\", # the city\n  'Iron''s [[Watch]]']\n---\n")
	if !slices.Equal(guard.Props["members"].([]string), []string{"King's Guard", "[[Varn]]", "Iron's [[Watch]]"}) || !slices.Equal(guard.Links, []string{"Varn", "Watch"}) {
		t.Errorf("members %v, links %v", guard.Props["members"], guard.Links)
	}
	if _, err := NewDossier(root, "character", "Pitch", "", nil); err == nil {
		t.Error("a character took the Pitch's name")
	}
	os.MkdirAll(filepath.Join(root, "people"), 0o755)
	os.WriteFile(filepath.Join(root, "people", "Mara.md"), []byte("mine"), 0o644)
	os.Symlink(filepath.Join(root, "people"), filepath.Join(root, Dir, "Characters"))
	for _, name := range []string{"Mara", "Kael"} { // over one, or a new one Notes wouldn't see
		if _, err := NewDossier(root, "character", name, "", nil); err == nil {
			t.Errorf("%s written through a link", name)
		}
	}
	if data, _ := os.ReadFile(filepath.Join(root, "people", "Mara.md")); string(data) != "mine" {
		t.Errorf("Mara: %q", data)
	}
}

// A property's name is what YAML reads: a dossier's own can't be given with a
// space after it, and a template's quoted names are its names.
func TestDossierPropertiesAreNamedAsYAMLReadsThem(t *testing.T) {
	root := t.TempDir()
	for _, key := range []string{"type ", "title ", "permalink ", "home "} {
		if _, err := NewDossier(root, "item", "Sword", "", map[string]string{key: "x"}); err == nil {
			t.Errorf("%q was taken", key)
		}
	}
	if _, err := os.Stat(filepath.Join(root, Dir, "Items", "Sword.md")); err == nil {
		t.Error("a dossier was written")
	}
	os.MkdirAll(filepath.Join(root, Dir, Templates), 0o755)
	os.WriteFile(filepath.Join(root, Dir, Templates, "Characters.md"), []byte("---\n\"type\": character\n'home': \"[[Old]]\"\nrole: \n---\n# {{title}}\n"), 0o644)
	if k := Kinds(root)[0]; k.Name != "character" || !slices.Equal(k.Props, []string{"home", "role"}) {
		t.Errorf("kind: %+v", k)
	}
	n, err := NewDossier(root, "character", "Mara", "", map[string]string{"home": "[[New]]", "Home town": "[[Irongate]]", "größe": "1,8 m"})
	if err != nil {
		t.Fatal(err)
	}
	if n.Type != "character" || n.Props["home"] != "[[New]]" || n.Props["Home town"] != "[[Irongate]]" || n.Props["größe"] != "1,8 m" ||
		!slices.Equal(n.Links, []string{"New", "Irongate"}) || strings.Count(n.Text, "home") != 1 || strings.Count(n.Text, "type") != 1 {
		t.Errorf("dossier %+v:\n%s", n, n.Text)
	}
	if base, _ := os.ReadFile(filepath.Join(root, Dir, "Characters.base")); !strings.Contains(string(base), "- note.home\n") {
		t.Errorf("base:\n%s", base)
	}
}

// A template whose type is empty (or only a comment) makes dossiers of its
// folder's kind, as its base looks for them.
func TestAnEmptyTypeIsTheFoldersKind(t *testing.T) {
	for _, line := range []string{"", "type:\n", "type: \"\"\n", "type: # none\n"} {
		root := t.TempDir()
		os.MkdirAll(filepath.Join(root, Dir, Templates), 0o755)
		os.WriteFile(filepath.Join(root, Dir, Templates, "Vehicles.md"), []byte("---\n"+line+"speed: 1\n---\n# {{title}}\n"), 0o644)
		n, err := NewDossier(root, "vehicles", "Barge", "", nil)
		if err != nil {
			t.Fatal(err)
		}
		base, _ := os.ReadFile(filepath.Join(root, Dir, "Vehicles.base"))
		if n.Type != "vehicles" || strings.Count(n.Text, "type:") != 1 || !strings.Contains(string(base), `'note.type == "vehicles"'`) {
			t.Errorf("%q: %+v\n%s", line, n, base)
		}
	}
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, Dir, Templates), 0o755)
	os.WriteFile(filepath.Join(root, Dir, Templates, "Logs.md"), []byte("---\ntype: \"captain's log\"\n---\n"), 0o644)
	if n, err := NewDossier(root, "captain's log", "Day 1", "", nil); err != nil || n.Type != "captain's log" {
		t.Errorf("%+v %v", n, err)
	}
}

// A template that can't be read stops a dossier (none from the built-in
// instead); one changed while it waits for the lock is the one it uses.
func TestADossierIsMadeFromTheTemplateThere(t *testing.T) {
	if os.Geteuid() == 0 {
		t.Skip("root reads anything")
	}
	root := t.TempDir()
	dir := filepath.Join(root, Dir, Templates)
	os.MkdirAll(dir, 0o755)
	own := filepath.Join(dir, "Characters.md")
	os.WriteFile(own, []byte("---\ntype: character\nclass: rogue\n---\n# {{title}}\n\n## Tricks\n"), 0o644)
	for _, locked := range []string{own, dir} {
		os.Chmod(locked, 0)
		_, err := NewDossier(root, "character", "Mara", "", nil)
		os.Chmod(locked, 0o755)
		if err == nil || !strings.Contains(err.Error(), "can't be read") {
			t.Errorf("%s unreadable: %v", locked, err)
		}
		if _, err := os.Stat(filepath.Join(root, Dir, "Characters.base")); err == nil {
			t.Error("a base from the built-in")
		}
	}
	os.WriteFile(filepath.Join(dir, "Boats.md"), []byte("---\n  type: boat\n  speed: 1\n---\n"), 0o644)
	if _, err := NewDossier(root, "boat", "Skiff", "", nil); err == nil {
		t.Error("a dossier from an indented template") // its lines under ours wouldn't be YAML
	}
	os.WriteFile(filepath.Join(dir, "Ships.md"), []byte("---\nhome: &h \"[[Port]]\"\nseat: *h\n---\n"), 0o644)
	if _, err := NewDossier(root, "ships", "Gull", "", map[string]string{"home": "[[Bay]]"}); err == nil {
		t.Error("a dossier with an alias of nothing")
	}
	if _, err := os.Stat(filepath.Join(root, Dir, "Ships.base")); err == nil {
		t.Error("a base for a dossier not written")
	}
	n, err := NewDossier(root, "character", "Mara", "", nil)
	if err != nil || n.Props["class"] != "rogue" || !strings.Contains(n.Text, "## Tricks") {
		t.Errorf("readable again: %+v %v", n, err)
	}
	release, err := gamelock.Hold()
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan Note)
	go func() {
		n, err := NewDossier(root, "character", "Kael", "", nil)
		if err != nil {
			t.Error(err)
		}
		done <- n
	}()
	time.Sleep(50 * time.Millisecond)
	os.WriteFile(own, []byte("---\ntype: character\nclass: bard\n---\n# {{title}}\n"), 0o644)
	release()
	if n := <-done; n.Props["class"] != "bard" {
		t.Errorf("Kael: %v", n.Props)
	}
	away := t.TempDir() // templates through a link out of the game aren't read
	os.WriteFile(filepath.Join(away, "Spies.md"), []byte("---\ntype: spy\n---\nsecret\n"), 0o644)
	os.Rename(dir, dir+".old")
	os.Symlink(away, dir)
	if _, err := NewDossier(root, "spy", "Vex", "", nil); err == nil || !strings.Contains(err.Error(), "out of the game") {
		t.Errorf("a template from outside: %v", err)
	}
}

// Values YAML reads over lines: a scalar or a list under its name, folded
// text, a comment alone, an empty item; a default's blank lines kept.
func TestPropertiesAreYAMLsValues(t *testing.T) {
	n := parse("design/x.md", "---\nhome:\n  \"[[Irongate]]\"\nlore: >\n  Born in\n  [[Varn]].\nallies:\n  [\"[[Kael]]\", \"pilot\"]\n"+
		"role: # later\nflow: [\"\", \"pilot\"]\nblock:\n  - \"\"\n  - pilot\nseat: [[Old Keep]]\n---\n")
	want := map[string]any{"home": "[[Irongate]]", "lore": "Born in [[Varn]].\n", "allies": []string{"[[Kael]]", "pilot"}, "role": "",
		"flow": []string{"", "pilot"}, "block": []string{"", "pilot"}, "seat": "[[Old Keep]]"}
	for k, v := range want {
		if got := n.Props[k]; fmt.Sprint(got) != fmt.Sprint(v) {
			t.Errorf("%s: %q, want %q", k, got, v)
		}
	}
	if !slices.Equal(n.Links, []string{"Irongate", "Varn", "Kael", "Old Keep"}) {
		t.Errorf("links: %v", n.Links)
	}
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, Dir, Templates), 0o755)
	os.WriteFile(filepath.Join(root, Dir, Templates, "Lore.md"), []byte("---\ntext: |\n  One.\n\n  Two.\nera: \n---\n"), 0o644)
	if d, err := NewDossier(root, "lore", "Fall", "", nil); err != nil || d.Props["text"] != "One.\n\nTwo.\n" {
		t.Errorf("%q %v", d.Props["text"], err)
	}
}
