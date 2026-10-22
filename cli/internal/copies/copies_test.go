package copies

import (
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

func makeGame(t *testing.T, dir string) {
	t.Helper()
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, ".jm.json"), []byte(`{}`), 0o644); err != nil {
		t.Fatal(err)
	}
}

// A variant's or sandbox's folder says whose copy it is; a game that only sits
// under such a path, or a sandbox's subfolder, doesn't.
func TestAFolderSaysWhoseCopyItIs(t *testing.T) {
	root := t.TempDir()
	game := filepath.Join(root, "repo", "games", "pip")
	for _, d := range []string{game, filepath.Join(game, ".jm", "sandboxes", "s"), filepath.Join(game, ".jm", "variants", "v"), filepath.Join(game, ".jm", "variants", "v", "games", "pip"),
		filepath.Join(root, "archive", ".jm", "variants", "Mine")} {
		makeGame(t, d)
	}
	for dir, want := range map[string]Copy{
		filepath.Join(game, ".jm", "sandboxes", "s"):                {game, Sandbox, "s"},
		filepath.Join(game, ".jm", "variants", "v"):                 {game, Variant, "v"},
		filepath.Join(game, ".jm", "variants", "v", "games", "pip"): {}, // no repository: the variant is its folder
		filepath.Join(game, ".jm", "sandboxes", "s", "scenes"):      {},
		filepath.Join(game, ".jm", "sandboxes", "gone"):             {game, Sandbox, "gone"}, // dropped: still its game's
		filepath.Join(root, "archive", ".jm", "variants", "Mine"):   {},
		game: {},
	} {
		if got, ok := Of(dir); got != want || ok != (want != Copy{}) {
			t.Errorf("%s: got %+v %v, want %+v", dir, got, ok, want)
		}
	}
}

// A variant of a game deep in its repository is the whole repository checked
// out: the game's library beside it ("../shared") is found beside the game,
// not in the worktree (where it's missing, or an older committed copy).
func TestAVariantFindsWhatsBesideItsGame(t *testing.T) {
	if _, err := exec.LookPath("git"); err != nil {
		t.Skip("no git")
	}
	repo := t.TempDir()
	game := filepath.Join(repo, "game")
	makeGame(t, game)
	c := exec.Command("git", "init", "-q")
	c.Dir = repo
	if out, err := c.CombinedOutput(); err != nil {
		t.Fatalf("git init: %v %s", err, out)
	}
	variant := filepath.Join(Store(game, Variant), "try", "game")
	if got, want := Beside(variant, "../shared"), filepath.Join(repo, "shared"); got != want {
		t.Errorf("a variant's ../shared = %s, want %s", got, want)
	}
	if got, want := Beside(variant, "lib"), filepath.Join(variant, "lib"); got != want {
		t.Errorf("a variant's own lib = %s, want %s", got, want)
	}
	if got, want := Beside(game, "../shared"), filepath.Join(repo, "shared"); got != want {
		t.Errorf("the game's ../shared = %s, want %s", got, want)
	}
	abs := filepath.Join(t.TempDir(), "common")
	if got := Beside(variant, abs); got != abs {
		t.Errorf("an absolute library = %s, want %s", got, abs)
	}
}

// A store is made in the game's own .jm; one that's a link is refused, never followed.
func TestAStoreIsTheGamesOwn(t *testing.T) {
	game := t.TempDir()
	if dir, err := OpenStore(game, Variant, false); dir != "" || err != nil {
		t.Errorf("no store yet: %q, %v", dir, err)
	}
	dir, err := OpenStore(game, Sandbox, true)
	if err != nil || dir != Store(game, Sandbox) {
		t.Fatalf("made: %q, %v", dir, err)
	}
	if _, err := os.Stat(filepath.Join(game, ".jm", ".gitignore")); err != nil {
		t.Errorf(".jm doesn't ignore itself: %v", err)
	}
	if err := os.Symlink(t.TempDir(), Store(game, Variant)); err != nil {
		t.Skip(err)
	}
	for _, create := range []bool{false, true} {
		if dir, err := OpenStore(game, Variant, create); err == nil {
			t.Errorf("a linked store (create %v) was taken: %q", create, dir)
		}
	}
}

// In a repository, a variant's game is where the game sits in it: a variant of
// games/a, opened at its games/a, is a's; its games/b is not.
func TestAVariantsGameIsWhereTheGameSits(t *testing.T) {
	repo := t.TempDir()
	if out, err := exec.Command("git", "-C", repo, "init", "-q").CombinedOutput(); err != nil {
		t.Skipf("git: %v %s", err, out)
	}
	a := filepath.Join(repo, "games", "a")
	v := filepath.Join(Store(a, Variant), "v")
	for _, d := range []string{a, filepath.Join(repo, "games", "b"), filepath.Join(v, "games", "a"), filepath.Join(v, "games", "b")} {
		makeGame(t, d)
	}
	if c, _ := Of(filepath.Join(v, "games", "a")); c != (Copy{a, Variant, "v"}) {
		t.Errorf("a's variant: %+v", c)
	}
	if c, ok := Of(filepath.Join(v, "games", "b")); ok {
		t.Errorf("b in a's variant: %+v", c)
	}
}

// Where copies are kept is this package's: a folder spelled elsewhere is a
// second layout to drift (build once read a variant's library from the wrong one).
func TestOnlyThisPackageSpellsWhereCopiesAre(t *testing.T) {
	spelled := regexp.MustCompile(`"\.jm[/\\](variants|sandboxes)|(Join|Make)\([^)]*"(variants|sandboxes)"`)
	for _, top := range []string{"../../cmd", ".."} {
		filepath.WalkDir(top, func(p string, d fs.DirEntry, err error) error {
			if err != nil || d.IsDir() && (d.Name() == "copies" || d.Name() == "widget") {
				return fs.SkipDir
			}
			if !strings.HasSuffix(p, ".go") || strings.HasSuffix(p, "_test.go") {
				return nil
			}
			data, _ := os.ReadFile(p)
			for i, line := range strings.Split(string(data), "\n") {
				if spelled.MatchString(line) {
					t.Errorf("%s:%d spells where copies are (copies.Store): %s", p, i+1, strings.TrimSpace(line))
				}
			}
			return nil
		})
	}
}
