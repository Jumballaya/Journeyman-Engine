package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Files link by path (from the game's folder or the file's), atlas#region,
// a manifest's glob, and a script's sound name; build output isn't the game's.
func TestIndexGameFindsWhatUsesWhat(t *testing.T) {
	root := t.TempDir()
	for path, text := range map[string]string{
		".jm.json":                       `{"name":"G","entryScene":"scenes/a.scene.json","assets":["assets/sounds/**"]}`,
		"assets/sounds/more/far.wav":     "RIFF",
		"assets/maps/town.tsj":           `{"image":"tiles.png"}`,
		"assets/maps/tiles.png":          "x",
		"tiles.png":                      "x",
		"scenes/b.scene.json":            `{"entities":[]}`,
		"scenes/a.scene.json":            `{"entities":[{"name":"P","prefab":"assets/prefabs/p.prefab.json","children":[{"name":"C"}]}]}`,
		"assets/prefabs/p.prefab.json":   `{"components":{"SpriteComponent":{"texture":"assets/fx.atlas.json#orb"},"ScriptComponent":{"script":"assets/scripts/p.ts"}}}`,
		"assets/fx.atlas.json":           `{"regions":{"orb":"orb.png"}}`,
		"assets/orb.png":                 "not an image",
		"assets/scripts/p.ts":            `import { go } from "./util"; Audio.play("hit"); Scene.load("b");`,
		"assets/scripts/util.ts":         `export function go(): void {}`,
		"assets/sounds/hit.wav":          "RIFF",
		"assets/sounds/unused.wav":       "RIFF",
		"assets/lonely.png":              "x",
		"build/scenes/a.scene.json":      `{}`,
		"tests/golden/title/frame_1.png": "x",
	} {
		os.MkdirAll(filepath.Join(root, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(root, path), []byte(text), 0o644)
	}
	got := map[string]indexEntry{}
	for _, e := range indexGame(root) {
		got[e.Path] = e
	}
	if _, ok := got["build/scenes/a.scene.json"]; ok {
		t.Error("build output indexed")
	}
	for path, want := range map[string]string{
		"assets/prefabs/p.prefab.json": "scenes/a.scene.json",
		"assets/fx.atlas.json":         "assets/prefabs/p.prefab.json",
		"assets/orb.png":               "assets/fx.atlas.json",
		"assets/scripts/util.ts":       "assets/scripts/p.ts",
		"assets/sounds/hit.wav":        ".jm.json,assets/scripts/p.ts",
		"assets/sounds/unused.wav":     ".jm.json",
		"assets/lonely.png":            "",
		"assets/sounds/more/far.wav":   ".jm.json",
		"assets/maps/tiles.png":        "assets/maps/town.tsj",
		"tiles.png":                    "",
		"scenes/b.scene.json":          "assets/scripts/p.ts",
	} {
		if by := strings.Join(got[path].UsedBy, ","); by != want {
			t.Errorf("%s used by %q, want %q", path, by, want)
		}
	}
	if got["scenes/a.scene.json"].Detail != "2 entities" || got["tests/golden/title/frame_1.png"].Kind != "golden" {
		t.Errorf("details: %+v %+v", got["scenes/a.scene.json"], got["tests/golden/title/frame_1.png"])
	}
	if text := describeIndex("G", indexGame(root)); !strings.Contains(text, "Nothing uses 3 of them: assets/maps/town.tsj, assets/lonely.png, tiles.png.") {
		t.Errorf("summary: %s", text)
	}
}

// A check's JSON lines become its problems and a summary.
func TestHealthReadsChecksProblems(t *testing.T) {
	root := sceneGame(t)
	t.Chdir(root)
	r := sceneCall(t, `{"name":"health","arguments":{"checks":["tests","goldens","nope"]}}`)
	checks := r["structuredContent"].(map[string]any)["checks"].([]any)
	summaries := []string{}
	for _, c := range checks {
		summaries = append(summaries, c.(map[string]any)["summary"].(string))
	}
	if got := strings.Join(summaries, " | "); got != "no tests yet (tests/*.spec.ts) | no goldens yet (tests/golden/*.golden.json) | no such check (doctor, build, tests, goldens, world)" {
		t.Errorf("summaries: %s", got)
	}
}

// A broad glob over files the index doesn't know, or links out, links only what it indexed.
func TestIndexLinksOnlyWhatItIndexed(t *testing.T) {
	root, outside := t.TempDir(), t.TempDir()
	os.WriteFile(filepath.Join(outside, "secret.png"), []byte("x"), 0o644)
	for path, text := range map[string]string{
		".jm.json": `{"name":"G","assets":["assets/**"]}`, "assets/a.png": "x", "assets/readme.txt": "hi", "assets/env.d.ts": "",
		"plugins/journeyman/plugin.json": "{}", // the agents' plugin, not the game's
	} {
		os.MkdirAll(filepath.Join(root, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(root, path), []byte(text), 0o644)
	}
	os.Symlink(filepath.Join(outside, "secret.png"), filepath.Join(root, "assets", "link.png"))
	got := map[string]indexEntry{}
	for _, e := range indexGame(root) {
		got[e.Path] = e
	}
	if _, ok := got["plugins/journeyman/plugin.json"]; ok {
		t.Error("the agents' plugin is indexed")
	}
	if strings.Join(got[".jm.json"].Uses, ",") != "assets/a.png" || strings.Join(got["assets/a.png"].UsedBy, ",") != ".jm.json" {
		t.Errorf("manifest uses %v", got[".jm.json"].Uses)
	}
}

// An image uses the normal map beside it (the engine lights it so); an orphan normal map is unused.
func TestAnImageUsesItsNormalMap(t *testing.T) {
	root := t.TempDir()
	for path, text := range map[string]string{
		".jm.json": `{"name":"G","assets":["assets/hero.png"]}`, "assets/hero.png": "x", "assets/hero.normal.png": "x",
		"assets/fx.atlas.json": `{"sources":["assets/orb.png"]}`, "assets/orb.png": "x", "assets/orb.normal.png": "x",
		"assets/lone.normal.png": "x",
	} {
		os.MkdirAll(filepath.Join(root, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(root, path), []byte(text), 0o644)
	}
	got := map[string]indexEntry{}
	for _, e := range indexGame(root) {
		got[e.Path] = e
	}
	for normal, image := range map[string]string{"assets/hero.normal.png": "assets/hero.png", "assets/orb.normal.png": "assets/orb.png"} {
		if got[normal].Unused || strings.Join(got[normal].UsedBy, ",") != image {
			t.Errorf("%s: used by %v, unused %v", normal, got[normal].UsedBy, got[normal].Unused)
		}
	}
	if !got["assets/lone.normal.png"].Unused {
		t.Error("an orphan normal map is used")
	}
}

// UI and atlases name files from the game's folder; Tiled maps and Aseprite sheets from their own.
func TestEachFormatsPathsResolveAsItsLoader(t *testing.T) {
	root := t.TempDir()
	for path, text := range map[string]string{
		".jm.json":           `{"name":"G"}`,
		"assets/ui/hud.html": `<link rel="stylesheet" href="assets/theme.css"><img src="assets/ship.png">`,
		"assets/theme.css":   "", "assets/ui/assets/theme.css": "",
		"assets/ship.png": "x", "assets/ui/assets/ship.png": "x",
		"assets/fx.atlas.json": `{"sources":["assets/orb.png"]}`,
		"assets/orb.png":       "x", "assets/assets/orb.png": "x",
		"assets/maps/town.tsj":  `{"image":"tiles.png"}`,
		"assets/maps/tiles.png": "x", "tiles.png": "x",
		"art/kage.json": `{"meta":{"image":"kage.png"}}`,
		"art/kage.png":  "x", "kage.png": "x",
	} {
		os.MkdirAll(filepath.Join(root, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(root, path), []byte(text), 0o644)
	}
	got := map[string]indexEntry{}
	for _, e := range indexGame(root) {
		got[e.Path] = e
	}
	for file, want := range map[string]string{
		"assets/ui/hud.html": "assets/ship.png,assets/theme.css", "assets/fx.atlas.json": "assets/orb.png",
		"assets/maps/town.tsj": "assets/maps/tiles.png", "art/kage.json": "art/kage.png",
	} {
		if uses := strings.Join(got[file].Uses, ","); uses != want {
			t.Errorf("%s uses %q, want %q", file, uses, want)
		}
	}
}

// indexOf indexes a game of files (path: text) by path.
func indexOf(t *testing.T, files map[string]string) map[string]indexEntry {
	root := t.TempDir()
	for p, text := range files {
		os.MkdirAll(filepath.Join(root, filepath.Dir(p)), 0o755)
		os.WriteFile(filepath.Join(root, p), []byte(text), 0o644)
	}
	got := map[string]indexEntry{}
	for _, e := range indexGame(root) {
		got[e.Path] = e
	}
	return got
}

// A script names a resource by file name only in the call that loads it, and
// only of that call's kind: other strings of the same name use nothing.
func TestOnlyResourceCallsNameResources(t *testing.T) {
	jump := map[string]string{"sounds/jump.wav": "x", "prefabs/jump.prefab.json": "{}", "scenes/jump.scene.json": "{}", "shaders/jump.frag": "x"}
	game := func(script string) map[string]indexEntry {
		files := map[string]string{".jm.json": `{"name":"G"}`, "scripts/a.ts": script}
		for p, text := range jump {
			files[p] = text
		}
		return indexOf(t, files)
	}
	for _, inert := range []string{`const description = "jump"; if (Input.down("jump")) {}`, "// Audio.play(\"jump\")\n",
		`/* Scene.load("jump"); spawn("jump", 0, 0); PostEffect.custom("jump") */`, `const help = 'Audio.play("jump")';`, "const t = `spawn(\"jump\")`;"} {
		got := game(inert)
		for p := range jump {
			if !got[p].Unused || len(got[p].UsedBy) != 0 {
				t.Errorf("%s uses %s: %v", inert, p, got[p].UsedBy)
			}
		}
	}
	got := game("// Scene.load(\"jump\")\nconst help = 'spawn(\"jump\")'; /* x */ Audio.play(\"jump\");")
	if uses := strings.Join(got["scripts/a.ts"].Uses, ","); uses != "sounds/jump.wav" {
		t.Errorf("a real call beside inert ones uses %q", uses)
	}
	for call, want := range map[string]string{
		`Audio.play("jump")`: "sounds/jump.wav", `new Sound("jump")`: "sounds/jump.wav", `new Music('jump')`: "sounds/jump.wav",
		`spawn("jump", 0, 0)`: "prefabs/jump.prefab.json", `Net.spawnPlayer(p, "jump", 0, 0)`: "prefabs/jump.prefab.json",
		`Scene.load("jump")`: "scenes/jump.scene.json", `PostEffect.custom("jump")`: "shaders/jump.frag",
		`Scene.transition("jump", 0.5, "")`: "scenes/jump.scene.json", `Scene.transition("x", duration(), "jump")`: "shaders/jump.frag",
		`screen.goTo("jump")`: "scenes/jump.scene.json", `screen.goTo("x", "jump")`: "shaders/jump.frag",
		`Net.spawnPlayer(Net.players()[0], "jump", 0, 0)`: "prefabs/jump.prefab.json", `Audio.play ("jump")`: "sounds/jump.wav", "const l = `${spawn(\"jump\", 0, 0).index}`;": "prefabs/jump.prefab.json", `const help = 'spawn('; spawn("jump", 0, 0);`: "prefabs/jump.prefab.json", `new Music(boss ? "jump" : "x")`: "sounds/jump.wav",
	} {
		got := game(call)
		if uses := strings.Join(got["scripts/a.ts"].Uses, ","); uses != want || strings.Join(got[want].UsedBy, ",") != "scripts/a.ts" {
			t.Errorf("%s uses %q, want %q", call, uses, want)
		}
	}
}

// A quoted path with spaces in it, in HTML or CSS, names its file.
func TestQuotedPathsKeepTheirSpaces(t *testing.T) {
	got := indexOf(t, map[string]string{
		".jm.json":            `{"name":"G"}`,
		"assets/ui/hud.html":  `<link rel="stylesheet" href='assets/my theme.css'><div class=""><img src="assets/the ship.png"></div><p style="background:url('assets/styled.png')"></p>`,
		"assets/my theme.css": `@font-face { src: url("assets/big font.ttf") } .x { background: url('assets/a b.png') } .y { background: url(assets/plain.png) }`,
		"assets/the ship.png": "x", "assets/big font.ttf": "x", "assets/a b.png": "x", "assets/plain.png": "x", "assets/styled.png": "x", "assets/orphan.png": "x",
	})
	for _, p := range []string{"assets/my theme.css", "assets/the ship.png", "assets/big font.ttf", "assets/a b.png", "assets/plain.png", "assets/styled.png"} {
		if got[p].Unused {
			t.Errorf("%s is unused", p)
		}
	}
	if !got["assets/orphan.png"].Unused {
		t.Error("the orphan is used")
	}
}
