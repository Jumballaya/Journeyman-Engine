package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

// builtFrom gives the game at root a build made from its sources as they are.
func builtFrom(t *testing.T, root string) {
	t.Helper()
	os.MkdirAll(filepath.Join(root, "build"), 0o755)
	os.WriteFile(filepath.Join(root, "build", ".jm.json"), []byte(`{}`), 0o644)
	if err := recordSources(filepath.Join(root, "build"), buildSources(root)); err != nil {
		t.Fatal(err)
	}
}

func TestBuildSourcesAreWhatTheBuildReadsNotWhatRunsWrite(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{"name": "T", "entryScene": "scenes/main.scene.json", "scenes": ["scenes/main.scene.json"], "assets": ["assets/game/**"]}`), 0o644)
	for _, f := range []string{"scenes/main.scene.json", "assets/sources/ship.png", "assets/scripts/package.json", "build/a.png", "logs/engine.log",
		"frames/frame_00060.png", ".jm/plays/x", "assets/scripts/node_modules/m.js"} {
		os.MkdirAll(filepath.Dir(filepath.Join(root, f)), 0o755)
		os.WriteFile(filepath.Join(root, f), []byte("x"), 0o644)
	}
	var there []string
	for key, stamp := range buildSources(root) {
		if stamp != "" {
			there = append(there, key)
		}
	}
	if got := len(there); got != 4 {
		t.Errorf("sources there: %v, want the manifest, the scene, the atlas source image and the scripts' package.json", there)
	}
}

func TestABuildIsStaleWhenItsSourcesDiffer(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{"assets": ["assets/**", "extra/*.json"]}`), 0o644)
	os.MkdirAll(filepath.Join(root, "assets", "scripts", "node_modules"), 0o755)
	os.MkdirAll(filepath.Join(root, "extra"), 0o755)
	os.WriteFile(filepath.Join(root, "extra", "a.json"), nil, 0o644)
	if !buildIsStale(root) {
		t.Error("no build isn't stale")
	}
	builtFrom(t, root)
	if buildIsStale(root) {
		t.Error("a build of the sources as they are is stale")
	}
	os.WriteFile(filepath.Join(root, "assets", "scripts", "node_modules", "x.js"), nil, 0o644)
	os.WriteFile(filepath.Join(root, "build", "logs.txt"), nil, 0o644)
	if buildIsStale(root) {
		t.Error("node_modules or the build itself counts as a source")
	}
	later := time.Now().Add(time.Minute)
	for _, change := range []struct {
		what string
		do   func()
	}{
		{"an edited script", func() {
			os.WriteFile(filepath.Join(root, "assets", "player.ts"), nil, 0o644)
			builtFrom(t, root)
			os.Chtimes(filepath.Join(root, "assets", "player.ts"), later, later)
		}},
		{"a new asset", func() { os.WriteFile(filepath.Join(root, "assets", "new.json"), nil, 0o644) }},
		{"an asset gone that a glob outside assets/ matched", func() { os.Remove(filepath.Join(root, "extra", "a.json")) }},
		{"a normal map made for an image", func() {
			os.WriteFile(filepath.Join(root, "assets", "ship.png"), nil, 0o644)
			builtFrom(t, root)
			os.WriteFile(filepath.Join(root, "assets", "ship_n.png"), nil, 0o644)
		}},
	} {
		builtFrom(t, root)
		change.do()
		if !buildIsStale(root) {
			t.Errorf("%s doesn't make the build stale", change.what)
		}
	}

	// A script library beside the game (../common), and what a link to one leads to, are sources too.
	game := filepath.Join(t.TempDir(), "game")
	common := filepath.Join(filepath.Dir(game), "common")
	os.MkdirAll(game, 0o755)
	os.MkdirAll(common, 0o755)
	os.WriteFile(filepath.Join(game, ".jm.json"), []byte(`{"scriptLibraries": {"common": "../common"}}`), 0o644)
	os.WriteFile(filepath.Join(common, "util.ts"), nil, 0o644)
	builtFrom(t, game)
	if buildIsStale(game) {
		t.Error("a build of its library as it is is stale")
	}
	os.Chtimes(filepath.Join(common, "util.ts"), later, later)
	if !buildIsStale(game) {
		t.Error("an edited library script doesn't make the build stale")
	}
	shared := filepath.Join(t.TempDir(), "shared")
	os.MkdirAll(shared, 0o755)
	os.WriteFile(filepath.Join(shared, "util.ts"), nil, 0o644)
	os.Remove(filepath.Join(common, "util.ts"))
	os.Remove(common)
	if os.Symlink(shared, common) == nil {
		builtFrom(t, game)
		if buildIsStale(game) {
			t.Error("a build of its linked library as it is is stale")
		}
		os.Chtimes(filepath.Join(shared, "util.ts"), later, later)
		if !buildIsStale(game) {
			t.Error("an edit in a linked library's folder doesn't make the build stale")
		}
		builtFrom(t, game)
		os.Remove(common)
		os.Symlink(t.TempDir(), common) // pointed elsewhere
		if !buildIsStale(game) {
			t.Error("a library link pointed elsewhere after the build doesn't make it stale")
		}
	}
}
