package vcs

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// game is a game inside a bigger repository's folder (not yet in git), with
// git's settings kept away from the person's.
func game(t *testing.T) (repo, root string) {
	t.Helper()
	if _, err := exec.LookPath("git"); err != nil {
		t.Skip("no git")
	}
	t.Setenv("GIT_CONFIG_GLOBAL", os.DevNull)
	t.Setenv("GIT_CONFIG_NOSYSTEM", "1")
	repo = t.TempDir()
	root = filepath.Join(repo, "games", "pip")
	put(t, root, ".jm.json", `{"name":"Pip"}`)
	put(t, root, "scenes/main.scene.json", `{"entities":[]}`)
	put(t, repo, "other.txt", "not the game's")
	return repo, root
}

func put(t *testing.T, root, path, text string) {
	t.Helper()
	os.MkdirAll(filepath.Dir(filepath.Join(root, path)), 0o755)
	if err := os.WriteFile(filepath.Join(root, path), []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestAGamesHistory(t *testing.T) {
	repo, root := game(t)
	if _, err := Log(root, 1); !errors.Is(err, ErrNoGit) {
		t.Errorf("log outside git: %v", err)
	}
	if made, err := Init(root); err != nil || !made {
		t.Fatalf("init: %v %v", made, err)
	}
	if made, _ := Init(root); made {
		t.Error("init again made another repository")
	}
	if _, err := Commit(root, "x"); !errors.Is(err, ErrNothing) {
		t.Errorf("commit with nothing changed: %v", err)
	}
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"A"}]}`)
	put(t, root, "assets/scripts/a b.ts", "// new")
	if got := Status(root); got["scenes/main.scene.json"] != "M" || got["assets/scripts/a b.ts"] != "?" || len(got) != 2 {
		t.Errorf("status: %v", got)
	}
	diff, err := Diff(root, "", "")
	if err != nil || !strings.Contains(diff, `+{"entities":[{"name":"A"}]}`) || !strings.Contains(diff, "+// new") {
		t.Errorf("diff (%v):\n%s", err, diff)
	}
	v, err := Commit(root, "")
	if err != nil || v.Subject != "Change a b.ts, main.scene.json" || v.Author != "Journeyman" || v.Files["assets/scripts/a b.ts"] != "A" || v.Files["scenes/main.scene.json"] != "M" {
		t.Fatalf("commit: %+v %v", v, err)
	}
	log, _ := Log(root, 10)
	if len(log) != 2 || log[1].Subject != "New game" || log[0].Hash != v.Hash || len(log[1].Files) != 2 {
		t.Errorf("log: %+v", log)
	}
	if string(Show(root, log[1].Hash, "scenes/main.scene.json")) != `{"entities":[]}` || Show(root, log[1].Hash, "assets/scripts/a b.ts") != nil {
		t.Error("show: files as they were")
	}
	if _, err := os.Stat(filepath.Join(repo, ".git")); err == nil {
		t.Error("the game's repository was made in the folder above it")
	}
}

func TestRestoreKeepsWhatItWouldLose(t *testing.T) {
	_, root := game(t)
	Init(root)
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"B"}]}`)
	first, _ := Commit(root, "B")
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"unsaved"}]}`)
	log, _ := Log(root, 2)
	backup, err := Restore(root, log[1].Hash, []string{"."})
	if err != nil || backup == "" {
		t.Fatalf("restore: %q %v", backup, err)
	}
	if data, _ := os.ReadFile(filepath.Join(root, "scenes/main.scene.json")); string(data) != `{"entities":[]}` {
		t.Errorf("restored: %s", data)
	}
	if out, _ := git(root, "stash", "show", "-p"); !strings.Contains(out, "unsaved") {
		t.Errorf("the unsaved change isn't in the stash:\n%s", out)
	}
	if backup, err := Restore(root, first.Hash, []string{"scenes/main.scene.json"}); err != nil || backup == "" {
		t.Errorf("restore a file: %q %v", backup, err)
	}
	if backup, _ := Restore(root, "", []string{"scenes/main.scene.json"}); backup != "" {
		t.Errorf("nothing to lose, but a backup: %q", backup)
	}
	if _, err := Restore(root, "nope", []string{"."}); err == nil {
		t.Error("restored a version that isn't")
	}
}

// A game in a bigger repository: its paths are from the game, and a commit holds only the game's.
func TestAGameInABiggerRepository(t *testing.T) {
	repo, root := game(t)
	git(repo, "init", "-q")
	if made, err := Init(root); made || err != nil {
		t.Fatalf("init in a repository: %v %v", made, err)
	}
	v, err := Commit(root, "Pip")
	if err != nil || len(v.Files) != 2 || v.Files["scenes/main.scene.json"] != "A" {
		t.Fatalf("commit: %+v %v", v, err)
	}
	if out, _ := git(repo, "status", "--porcelain"); !strings.Contains(out, "?? other.txt") {
		t.Errorf("the rest of the repository was committed: %q", out)
	}
}

func TestRestoreAndDiffStayInTheGame(t *testing.T) {
	repo, root := game(t)
	git(repo, "init", "-q")
	put(t, repo, "other.txt", "v1")
	git(repo, "add", "other.txt")
	Commit(root, "Pip")
	git(repo, append(identity(repo), "commit", "-qam", "other")...)
	put(t, repo, "other.txt", "mine")
	for _, paths := range [][]string{{":(top)other.txt"}, {"../../other.txt"}, {"/etc"}} {
		if _, err := Restore(root, "", paths); err == nil {
			t.Errorf("restored %v", paths)
		}
	}
	if data, _ := os.ReadFile(filepath.Join(repo, "other.txt")); string(data) != "mine" {
		t.Errorf("a file outside the game was restored: %s", data)
	}
	if _, err := Diff(root, "--output=.jm.json", ""); err == nil {
		t.Error("a version read as an option")
	}
	if data, _ := os.ReadFile(filepath.Join(root, ".jm.json")); string(data) != `{"name":"Pip"}` {
		t.Errorf("the manifest was written: %s", data)
	}
	// Ignored now, tracked then: not written over.
	put(t, root, "keep.txt", "v1")
	Commit(root, "keep")
	log, _ := Log(root, 1)
	os.Remove(filepath.Join(root, "keep.txt"))
	put(t, root, ".gitignore", "keep.txt\n")
	Commit(root, "ignore keep")
	put(t, root, "keep.txt", "new work")
	if _, err := Restore(root, log[0].Hash, []string{"."}); err == nil {
		t.Error("restored over an ignored file")
	}
	if data, _ := os.ReadFile(filepath.Join(root, "keep.txt")); string(data) != "new work" {
		t.Errorf("ignored work lost: %s", data)
	}
}

// A version whose file is now an ignored folder of the person's (or the other way
// around) isn't restored over it: git's stash wouldn't keep what it would lose.
func TestRestoreLeavesIgnoredFilesInItsWayAlone(t *testing.T) {
	_, root := game(t)
	Init(root)
	put(t, root, "slot", "old file")
	put(t, root, "dir/inner.txt", "old inner")
	old, _ := Commit(root, "a file and a folder")
	os.Remove(filepath.Join(root, "slot"))
	os.RemoveAll(filepath.Join(root, "dir"))
	put(t, root, ".gitignore", "slot/\ndir\n")
	Commit(root, "now ignored")
	put(t, root, "slot/precious.txt", "mine")
	put(t, root, "dir", "mine too")
	for _, path := range []string{"slot", "dir", "dir/inner.txt", "."} {
		if _, err := Restore(root, old.Hash, []string{path}); err == nil {
			t.Errorf("restored %s over what git ignores", path)
		}
	}
	if data, _ := os.ReadFile(filepath.Join(root, "slot/precious.txt")); string(data) != "mine" {
		t.Errorf("slot/precious.txt: %q", data)
	}
	if data, _ := os.ReadFile(filepath.Join(root, "dir")); string(data) != "mine too" {
		t.Errorf("dir: %q", data)
	}
}

// "." is the whole game, new files too, as "" is.
func TestTheWholeGamesDiffHasNewFiles(t *testing.T) {
	_, root := game(t)
	Init(root)
	put(t, root, "assets/new.ts", "// brand new")
	whole, _ := Diff(root, "", "")
	dot, _ := Diff(root, "", ".")
	if !strings.Contains(dot, "brand new") || dot != whole {
		t.Errorf(". diff:\n%s\nwhole:\n%s", dot, whole)
	}
	if one, _ := Diff(root, "", "scenes/main.scene.json"); strings.Contains(one, "brand new") {
		t.Errorf("one file's diff has another: %s", one)
	}
}

// In a bigger repository, restoring the game leaves the repository's other files as they were: staged, changed or new.
func TestRestoreLeavesTheRestOfTheRepositoryAlone(t *testing.T) {
	repo, root := game(t)
	git(repo, "init", "-q")
	Init(root)
	put(t, repo, "other.txt", "committed")
	git(repo, "add", "other.txt")
	git(repo, append(identity(repo), "commit", "-q", "-m", "other")...)
	Commit(root, "Pip")
	put(t, repo, "other.txt", "staged")
	git(repo, "add", "other.txt")
	put(t, repo, "other.txt", "changed after staging")
	put(t, repo, "loose.txt", "new outside")
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"Mine"}]}`)
	for _, paths := range [][]string{{"."}, {"scenes"}, {"scenes/main.scene.json"}, {"./"}} {
		if _, err := Restore(root, "", paths); err != nil {
			t.Fatalf("%v: %v", paths, err)
		}
		if data, _ := os.ReadFile(filepath.Join(repo, "other.txt")); string(data) != "changed after staging" {
			t.Errorf("%v: other.txt is %q", paths, data)
		}
		if staged, _ := git(repo, "show", ":other.txt"); staged != "staged" {
			t.Errorf("%v: the staged other.txt is %q", paths, staged)
		}
		if _, err := os.Stat(filepath.Join(repo, "loose.txt")); err != nil {
			t.Errorf("%v: loose.txt went", paths)
		}
		put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"Mine"}]}`)
	}
}

// Ways of writing one new file's path give its diff alike.
func TestANewFilesDiffByAnyPathSpelling(t *testing.T) {
	_, root := game(t)
	Init(root)
	put(t, root, "assets/new.ts", "// brand new")
	want, _ := Diff(root, "", "assets/new.ts")
	for _, p := range []string{"./assets/new.ts", "assets/../assets/new.ts"} {
		if got, _ := Diff(root, "", p); got != want || !strings.Contains(got, "brand new") {
			t.Errorf("%s:\n%s", p, got)
		}
	}
}
