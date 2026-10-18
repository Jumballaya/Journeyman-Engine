package memory

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
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
