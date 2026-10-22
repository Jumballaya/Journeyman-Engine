package main

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
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
	for _, f := range []string{"scenes/main.scene.json", "assets/sources/ship.png", "assets/scripts/package.json", "art/hero.png", "assets/frames/run.png",
		"build/a.png", "build.next/a.png", "logs/engine.log", "frames/frame_00060.png", ".jm/plays/x", ".git/HEAD", "assets/scripts/node_modules/m.js", "old.jm"} {
		os.MkdirAll(filepath.Dir(filepath.Join(root, f)), 0o755)
		os.WriteFile(filepath.Join(root, f), []byte("x"), 0o644)
	}
	var there []string
	for key, stamp := range buildSources(root) {
		if stamp != "" {
			there = append(there, key)
		}
	}
	slices.Sort(there)
	if got := strings.Join(there, " "); got != ".jm.json art/hero.png assets/frames/run.png assets/scripts/package.json assets/sources/ship.png scenes/main.scene.json" {
		t.Errorf("sources there: %s", got)
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
	os.Remove(filepath.Join(root, "build", ".jm.json"))
	if !buildIsStale(root) {
		t.Error("a build without its manifest isn't stale")
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
		{"a normal map made for an image", func() { os.WriteFile(filepath.Join(root, "assets", "ship_n.png"), nil, 0o644) }},
		{"an edited image an atlas outside assets/ names", func() {
			os.MkdirAll(filepath.Join(root, "art"), 0o755)
			os.WriteFile(filepath.Join(root, "art", "hero.png"), nil, 0o644)
			builtFrom(t, root)
			os.Chtimes(filepath.Join(root, "art", "hero.png"), later, later)
		}},
	} {
		builtFrom(t, root)
		change.do()
		if !buildIsStale(root) {
			t.Errorf("%s doesn't make the build stale", change.what)
		}
	}

	// A library that is the game itself: what a build writes doesn't make it stale.
	self := t.TempDir()
	os.WriteFile(filepath.Join(self, ".jm.json"), []byte(`{"scriptLibraries": {"me": "."}}`), 0o644)
	builtFrom(t, self)
	os.WriteFile(filepath.Join(self, "build", "out.wasm"), nil, 0o644)
	os.MkdirAll(filepath.Join(self, "logs"), 0o755)
	os.WriteFile(filepath.Join(self, "logs", "engine.log"), nil, 0o644)
	if buildIsStale(self) {
		t.Error("a build of a game that is its own library is stale from its own output")
	}

	// A script library beside the game (../common), its hidden folders, and what a link to one leads to, are sources too.
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
	os.MkdirAll(filepath.Join(common, ".internal"), 0o755)
	builtFrom(t, game)
	os.WriteFile(filepath.Join(common, ".internal", "x.ts"), nil, 0o644)
	if !buildIsStale(game) {
		t.Error("a library's hidden folder (copied with it) isn't a source")
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
