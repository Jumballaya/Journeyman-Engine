package formats

import (
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"testing"
)

func TestTheLongestEndingNamesTheFormat(t *testing.T) {
	for name, want := range map[string]string{
		"scenes/main.scene.json": "scene", "assets/data/items.json": "data", "a/b.prefab.json": "prefab",
		"input.bindings.json": "bindings", ".jm.json": "manifest", "assets/scripts/hero.ts": "script",
		"tests/hero.spec.ts": "", "assets/ui/hud.ui.html": "ui", "assets/sprites/HERO.PNG": "", "assets/scripts/Player.TS": "", // jm build's case
	} {
		if f, _ := Of(name); f.Archive != want {
			t.Errorf("%s ships as %q, want %q", name, f.Archive, want)
		}
	}
	if _, ok := Of("notes.rtf"); ok {
		t.Error("a .rtf is no format of a game's")
	}
}

// What a tool offers or takes is something the game ships, or (Aseprite) bakes.
func TestOfferedFormatsShip(t *testing.T) {
	for _, f := range All(func(f Format) bool { return f.Pick != "" || f.Upload }) {
		if f.Archive == "" && f.Kind != "texture" {
			t.Errorf("%s is offered but never shipped", f.Suffix)
		}
	}
}

// Any JSON the game uses can be uploaded, as asset_add always took it.
func TestEveryJSONOfTheGamesUploads(t *testing.T) {
	for _, name := range []string{"a.json", "a.scene.json", "a.prefab.json", "a.atlas.json", "a.bindings.json", "a.tmj", "a.tsj"} {
		if f, _ := Of(name); !f.Upload || len(f.UploadMIME) == 0 {
			t.Errorf("%s: upload %v, %v", name, f.Upload, f.UploadMIME)
		}
	}
}

// A text format is served as its content type; a binary one isn't text.
func TestTextFormatsAreServedAsTheirType(t *testing.T) {
	for name, want := range map[string]string{
		"scenes/a.scene.json": "application/json", "assets/maps/m.tmj": "application/json", "assets/scripts/hero.ts": "text/x-typescript",
		"assets/shaders/glow.vert": "text/plain", "assets/shaders/glow.frag": "text/plain", "assets/ui/hud.ui.html": "text/html",
		"design/notes.md": "text/markdown", "design/notes.txt": "text/plain", "assets/maps/m.tmx": "application/xml", "assets/hero.png": "", "assets/hero.aseprite": "",
	} {
		if f, _ := Of(name); f.TextMIME != want {
			t.Errorf("%s is served as %q, want %q", name, f.TextMIME, want)
		}
	}
}

// The editor's own table (editor/Project.cpp assetKindOf) knows the formats a
// game ships: a format added here is one it shows too.
func TestTheEditorKnowsWhatShips(t *testing.T) {
	src, err := os.ReadFile(filepath.Join("..", "..", "..", "editor", "Project.cpp"))
	if err != nil {
		t.Skip("no editor source here:", err)
	}
	table := regexp.MustCompile(`(?s)kBySuffix\[\] = \{(.*?)\};`).FindSubmatch(src)
	if table == nil {
		t.Fatal("editor/Project.cpp has no kBySuffix table")
	}
	var editor []string
	for _, m := range regexp.MustCompile(`\{"(\.[a-z0-9.]+)", AssetKind::`).FindAllSubmatch(table[1], -1) {
		editor = append(editor, string(m[1]))
	}
	var shipped []string
	for _, f := range All(func(f Format) bool { return f.Archive != "" && f.Archive != "manifest" }) {
		shipped = append(shipped, f.Suffix)
	}
	slices.Sort(editor)
	slices.Sort(shipped)
	if !slices.Equal(editor, shipped) {
		t.Errorf("the editor knows %v; the game ships %v", editor, shipped)
	}
}
