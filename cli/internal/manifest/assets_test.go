package manifest

import (
	"reflect"
	"slices"
	"testing"
	"testing/fstest"
)

func TestExpandAssetsMatchesGlobsAndKeepsPlainPaths(t *testing.T) {
	files := fstest.MapFS{
		"assets/prefabs/a.prefab.json":        {},
		"assets/prefabs/b.prefab.json":        {},
		"assets/scripts/hero.ts":              {},
		"assets/scripts/lib/body.ts":          {},
		"assets/scripts/node_modules/x/y.ts":  {},
		"assets/maps/town.txt":                {},
		"assets/maps/deep/field.txt":          {},
		"build/assets/prefabs/c.prefab.json":  {},
		".cache/assets/prefabs/d.prefab.json": {},
	}
	got, err := ExpandAssets(files, []string{
		"assets/input.bindings.json",
		"assets/prefabs/*.prefab.json",
		"assets/scripts/*.ts",
		"assets/maps/**",
		"assets/prefabs/a.prefab.json", // already matched: not repeated
	})
	if err != nil {
		t.Fatal(err)
	}
	want := []string{
		"assets/input.bindings.json",
		"assets/prefabs/a.prefab.json",
		"assets/prefabs/b.prefab.json",
		"assets/scripts/hero.ts",
		"assets/maps/deep/field.txt",
		"assets/maps/town.txt",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("ExpandAssets:\n got %v\nwant %v", got, want)
	}
}

func TestDoubleStarMatchesAnyDepth(t *testing.T) {
	re := globRegexp("assets/**/*.wav")
	for _, p := range []string{"assets/a.wav", "assets/sfx/a.wav", "assets/sfx/ui/a.wav"} {
		if !re.MatchString(p) {
			t.Errorf("%s should match", p)
		}
	}
	if re.MatchString("assets/a.ogg") {
		t.Error("assets/a.ogg should not match")
	}
}

func TestGlobsSkipTheScriptsToolingFiles(t *testing.T) {
	files := fstest.MapFS{
		"assets/scripts/player.ts":     {},
		"assets/scripts/package.json":  {},
		"assets/scripts/tsconfig.json": {},
		"assets/scripts/asconfig.json": {},
		"assets/data/levels.json":      {},
	}
	got, err := ExpandAssets(files, []string{"assets/**"})
	if err != nil {
		t.Fatal(err)
	}
	if want := []string{"assets/data/levels.json", "assets/scripts/player.ts"}; !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

// A glob of everything takes the game's files, not what jm makes at its top.
func TestAGlobOfEverythingLeavesWhatJmMakes(t *testing.T) {
	game := fstest.MapFS{}
	for _, f := range []string{"assets/frames/run.png", "assets/build/a.png", "assets/dist/b.png", "scenes/a.scene.json",
		"build/x.png", "BUILD/y.png", "build.next/z.png", "logs/engine.log", "Frames/f.png", "dist/game.jm", ".jm/plays/p", "assets/scripts/node_modules/m.ts"} {
		game[f] = &fstest.MapFile{Data: []byte("x")}
	}
	got, err := ExpandAssets(game, []string{"**"})
	if err != nil {
		t.Fatal(err)
	}
	slices.Sort(got)
	if want := []string{"assets/build/a.png", "assets/dist/b.png", "assets/frames/run.png", "scenes/a.scene.json"}; !slices.Equal(got, want) {
		t.Errorf("** = %v, want %v", got, want)
	}
}
