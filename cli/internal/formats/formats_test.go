package formats

import "testing"

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
	if _, ok := Of("notes.txt"); ok {
		t.Error("a .txt is no format of a game's")
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
		if f, _ := Of(name); !f.Upload || len(f.MIME) == 0 {
			t.Errorf("%s: upload %v, %v", name, f.Upload, f.MIME)
		}
	}
}
