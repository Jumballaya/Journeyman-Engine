package realpath

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestRelSaysWherePathsReallyLead(t *testing.T) {
	top := t.TempDir()
	game := filepath.Join(top, "game")
	outside := filepath.Join(top, "outside")
	for _, d := range []string{filepath.Join(game, "scenes", "deep"), outside} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	link := func(target, name string) {
		if err := os.Symlink(target, filepath.Join(game, name)); err != nil {
			t.Skip("no symlinks here:", err)
		}
	}
	link(outside, "out")
	link(filepath.Join(game, "scenes"), "in")
	link(filepath.Join(top, "nothing"), "dangling")
	link(filepath.Join(game, "scenes", "deep"), "deep")

	for _, c := range []struct {
		path, rel string
		ok        bool
	}{
		{".", ".", true},
		{"scenes/main.scene.json", "scenes/main.scene.json", true},
		{"scenes/new/later.json", "scenes/new/later.json", true}, // not made yet
		{filepath.Join(game, "scenes"), "scenes", true},
		{"in/a.json", "scenes/a.json", true},
		{"scenes/deep/../a.json", "scenes/a.json", true},
		{"deep/../a.json", "scenes/a.json", true}, // ".." after a link: from where it leads
		{"out/a.json", "", false},
		{"dangling", "", false},
		{"dangling/a.json", "", false},
		{"../outside", "", false},
		{"scenes/missing/../../../outside", "", false}, // ".." past what exists
		{"in/../../outside", "", false},
		{"out/../scenes", "", false}, // lexically game/scenes; out leads to outside/, so ../scenes is beside it
		{outside, "", false},
		{top, "", false},
	} {
		rel, ok := Rel(game, filepath.FromSlash(c.path))
		if rel != c.rel || ok != c.ok {
			t.Errorf("Rel(game, %q) = %q, %v; want %q, %v", c.path, rel, ok, c.rel, c.ok)
		}
	}
	if In(filepath.Join(game, "out"), filepath.Join(outside, "x")) != true {
		t.Error("a link to a folder holds what that folder does")
	}
	if Of(filepath.Join(game, "dangling", "x")) != "" {
		t.Error("Of through a link to nothing is nothing")
	}
}

func TestRelSeesACaseBlindDisksOtherSpelling(t *testing.T) {
	game := filepath.Join(t.TempDir(), "Game")
	if err := os.MkdirAll(filepath.Join(game, "scenes"), 0o755); err != nil {
		t.Fatal(err)
	}
	other := filepath.Join(filepath.Dir(game), "game")
	if _, err := os.Stat(other); err != nil {
		t.Skip("this disk tells Game from game")
	}
	rel, ok := Rel(game, filepath.Join(other, "scenes", "a.json"))
	if !ok || !strings.EqualFold(rel, "scenes/a.json") {
		t.Errorf("Rel through the other spelling = %q, %v", rel, ok)
	}
	if !In(other, game) {
		t.Error("game holds Game")
	}
}

func TestParentCutsAsWritten(t *testing.T) {
	sep := string(filepath.Separator)
	root := filepath.VolumeName(os.TempDir()) + sep
	for _, c := range []struct{ p, dir, base string }{
		{root + "a", root, "a"},
		{root + "a" + sep + ".." + sep + "b", root + "a" + sep + "..", "b"},
		{root + "a" + sep + "b" + sep, root + "a", "b"},
	} {
		dir, base, ok := parent(c.p)
		if !ok || dir != c.dir || base != c.base {
			t.Errorf("parent(%q) = %q, %q, %v", c.p, dir, base, ok)
		}
	}
	if _, _, ok := parent(root); ok {
		t.Error("the root has no parent")
	}
}
