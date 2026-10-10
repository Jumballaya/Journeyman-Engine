package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestMCPMakesAndOpensGamesFromNowhere(t *testing.T) {
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	wd, _ := os.Getwd()
	defer os.Chdir(wd)
	os.Chdir(t.TempDir()) // no game here, like Claude Desktop's server

	s := newMCPServer()
	call := func(name string, args map[string]any) toolResult {
		for _, tool := range s.tools {
			if tool.Name == name {
				return tool.run(args)
			}
		}
		t.Fatalf("no tool %s", name)
		return toolResult{}
	}
	if got := call("games", nil); !strings.Contains(got.Text, "No game open") || !strings.Contains(got.Text, "none yet") {
		t.Errorf("games before: %s", got.Text)
	}
	if got := call("new_game", map[string]any{"name": "Neon Vow"}); got.Failed {
		t.Fatal(got.Text)
	}
	cwd, _ := os.Getwd()
	if want, _ := filepath.EvalSymlinks(filepath.Join(home, "Neon Vow")); evalOr(cwd) != want || !exists(".jm.json") {
		t.Fatalf("new_game didn't open the new game: in %s", cwd)
	}
	if got := call("new_game", map[string]any{"name": "Neon Vow"}); !got.Failed {
		t.Error("a second game of the same name must be refused")
	}
	for _, bad := range []string{"..", ".hidden"} {
		if got := call("new_game", map[string]any{"name": bad}); !got.Failed {
			t.Errorf("new_game %q escaped the games folder: %s", bad, got.Text)
		}
	}

	os.Chdir(t.TempDir())
	if got := call("open_game", map[string]any{"game": "Neon Vow"}); got.Failed || !exists(".jm.json") {
		t.Fatalf("open_game by name: %s", got.Text)
	}
	if got := call("games", nil); !strings.Contains(got.Text, "Open: Neon Vow") || !strings.Contains(got.Text, `open_game "Neon Vow"`) {
		t.Errorf("games after: %s", got.Text)
	}
	if got := call("open_game", map[string]any{"game": "Nope"}); !got.Failed {
		t.Error("an unknown game must fail")
	}
}

func evalOr(p string) string {
	if r, err := filepath.EvalSymlinks(p); err == nil {
		return r
	}
	return p
}

func TestGamesFolderStaysPutWhenTheOpenGameMoves(t *testing.T) {
	wd, _ := os.Getwd()
	defer os.Chdir(wd)
	start := t.TempDir()
	os.Chdir(start)
	t.Setenv("JM_GAMES", "games") // relative: resolved once, at the start
	home := gamesHome()
	a, err := newGame(home, "A")
	if err != nil {
		t.Fatal(err)
	}
	os.Chdir(a)
	b, err := newGame(home, "B")
	if err != nil || filepath.Dir(b) != filepath.Dir(a) {
		t.Errorf("B landed in %s, not beside A (%v)", b, err)
	}
	// A's own build/ folder isn't the listed game "build".
	os.MkdirAll(filepath.Join(a, "build"), 0o755)
	os.WriteFile(filepath.Join(a, "build", ".jm.json"), []byte(`{"name":"built"}`), 0o644)
	if _, err := newGame(home, "build"); err != nil {
		t.Fatal(err)
	}
	if dir, _ := findGame(home, "build"); dir != filepath.Join(home, "build") {
		t.Errorf("open_game build opened %s", dir)
	}
}

func TestNewGameNeverRemovesAFolderItDidntMake(t *testing.T) {
	home := t.TempDir()
	mine := filepath.Join(home, "Taken")
	os.MkdirAll(mine, 0o755)
	os.WriteFile(filepath.Join(mine, "notes.txt"), []byte("keep"), 0o644)
	if _, err := newGame(home, "Taken"); err == nil {
		t.Error("an existing folder must be refused")
	}
	if !exists(filepath.Join(mine, "notes.txt")) {
		t.Error("deleted a folder it didn't make")
	}
}

func TestOpenGameTakesAListedNameOrAnAbsolutePath(t *testing.T) {
	root := t.TempDir()
	home := filepath.Join(root, "games")
	a, err := newGame(home, "A")
	if err != nil {
		t.Fatal(err)
	}
	for _, dir := range []string{filepath.Join(a, "build"), filepath.Join(root, "Outside"), home} {
		os.MkdirAll(dir, 0o755)
		os.WriteFile(filepath.Join(dir, ".jm.json"), []byte(`{"name":"x"}`), 0o644)
	}
	for _, bad := range []string{"A/build", "../Outside", ".", ".."} {
		if dir, err := findGame(home, bad); err == nil {
			t.Errorf("open_game %q opened %s", bad, dir)
		}
	}
	for _, good := range []string{"A", a} {
		if dir, err := findGame(home, good); err != nil || dir != a {
			t.Errorf("open_game %q: %s, %v", good, dir, err)
		}
	}
}
