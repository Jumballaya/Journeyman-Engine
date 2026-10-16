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
	if got := strings.Join(summaries, " | "); got != "no tests yet (tests/*.spec.ts) | no goldens yet (tests/golden/*.golden.json) | no such check (doctor, build, tests, goldens)" {
		t.Errorf("summaries: %s", got)
	}
}

// A broad glob over files the index doesn't know, or links out, links only what it indexed.
func TestIndexLinksOnlyWhatItIndexed(t *testing.T) {
	root, outside := t.TempDir(), t.TempDir()
	os.WriteFile(filepath.Join(outside, "secret.png"), []byte("x"), 0o644)
	for path, text := range map[string]string{
		".jm.json": `{"name":"G","assets":["assets/**"]}`, "assets/a.png": "x", "assets/readme.txt": "hi", "assets/env.d.ts": "",
	} {
		os.MkdirAll(filepath.Join(root, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(root, path), []byte(text), 0o644)
	}
	os.Symlink(filepath.Join(outside, "secret.png"), filepath.Join(root, "assets", "link.png"))
	got := map[string]indexEntry{}
	for _, e := range indexGame(root) {
		got[e.Path] = e
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
