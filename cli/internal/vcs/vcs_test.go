package vcs

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
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

// Discarding a new file stashes it: gone from the game, back with git stash pop.
func TestDiscardingANewFileKeepsItInTheStash(t *testing.T) {
	_, root := game(t)
	Init(root)
	put(t, root, "assets/scripts/bat.ts", "// bat\n")
	put(t, root, "levels/deep/new.json", "{}")
	for _, p := range []string{"assets/scripts/bat.ts", "levels/deep/new.json"} {
		backup, err := Restore(root, "", []string{p})
		if err != nil || backup == "" {
			t.Fatalf("%s: %q %v", p, backup, err)
		}
		if _, err := os.Stat(filepath.Join(root, p)); err == nil {
			t.Errorf("%s is still there", p)
		}
	}
	if _, err := git(root, "stash", "pop", "--quiet"); err != nil {
		t.Fatal(err)
	}
	if data, _ := os.ReadFile(filepath.Join(root, "levels/deep/new.json")); string(data) != "{}" {
		t.Errorf("new.json from the stash: %q", data)
	}
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"X"}]}`)
	if _, err := Restore(root, "", []string{"scenes/main.scene.json"}); err != nil {
		t.Errorf("a tracked change: %v", err)
	}
}

// A new file's diff is that file, all added.
func TestANewFilesDiffIsItAllAdded(t *testing.T) {
	_, root := game(t)
	Init(root)
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"X"}]}`)
	put(t, root, "assets/scripts/bat.ts", "// bat\n")
	d, _ := Diff(root, "", "assets/scripts/bat.ts")
	if !strings.Contains(d, "+// bat") || strings.Contains(d, "main.scene.json") {
		t.Errorf("diff:\n%s", d)
	}
}

// Going back takes away what came after, and a path written "./a" is a.
func TestRestoreRemovesLaterFilesAndReadsDotPaths(t *testing.T) {
	_, root := game(t)
	Init(root)
	log, _ := Log(root, 1)
	old := log[0].Hash
	put(t, root, "later.txt", "later")
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"New"}]}`)
	Commit(root, "later")
	if _, err := Restore(root, old, []string{"later.txt", "./scenes/main.scene.json"}); err != nil {
		t.Fatal(err)
	}
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"Again"}]}`)
	if _, err := Restore(root, old, []string{"./"}); err != nil {
		t.Fatal(err)
	}
	if onDisk(filepath.Join(root, "later.txt")) {
		t.Error("later.txt is still there")
	}
	if data, _ := os.ReadFile(filepath.Join(root, "scenes/main.scene.json")); string(data) != `{"entities":[]}` {
		t.Errorf("main.scene.json: %s", data)
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

// A restore holds the games' lock: it waits while someone else changes files.
func TestRestoreWaitsForTheGamesLock(t *testing.T) {
	_, root := game(t)
	if _, err := Commit(root, "first"); err != nil {
		t.Fatal(err)
	}
	put(t, root, "scenes/main.scene.json", `{"entities":[{"name":"Bat"}]}`)
	release, err := gamelock.Hold()
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan error)
	go func() { _, err := Restore(root, "", []string{"."}); done <- err }()
	select {
	case err := <-done:
		release()
		t.Fatalf("restored while the lock was held: %v", err)
	case <-time.After(200 * time.Millisecond):
	}
	release()
	if err := <-done; err != nil {
		t.Fatal(err)
	}
}

// Any byte but NUL in a subject or an author: the version still reads whole.
func TestLogReadsAnySubject(t *testing.T) {
	_, root := game(t)
	subject := "odd \x01 subject \x02"
	if _, err := Commit(root, subject); err != nil {
		t.Fatal(err)
	}
	log, err := Log(root, 5)
	if err != nil || len(log) != 1 {
		t.Fatalf("log: %v %v", log, err)
	}
	if v := log[0]; !fullHash(v.Hash) || v.Subject != subject || v.Files[".jm.json"] != "A" {
		t.Errorf("version: %+v", v)
	}
}

// A restore that fails puts the changes back from its own stash, which it keeps (it never drops one).
func TestAFailedRestorePutsChangesBack(t *testing.T) {
	_, root := game(t)
	if _, err := Commit(root, "first"); err != nil {
		t.Fatal(err)
	}
	put(t, root, "older.txt", "someone's") // an older stash of someone else's
	if _, err := git(root, "stash", "push", "--include-untracked", "-m", "theirs", "--", "older.txt"); err != nil {
		t.Fatal(err)
	}
	theirs, _ := git(root, "rev-parse", "refs/stash")
	put(t, root, "new.txt", "mine")
	if _, err := git(root, "add", "new.txt"); err != nil { // staged, not in that version: git restore refuses it
		t.Fatal(err)
	}
	backup, err := Restore(root, "", []string{"new.txt"})
	if err == nil || !strings.Contains(err.Error(), "new.txt") || !strings.Contains(err.Error(), "back as they were") {
		t.Fatalf("not git restore's refusal, put back: %v", err)
	}
	if !strings.Contains(backup, "git stash apply --index") {
		t.Errorf("the kept stash isn't said: %q", backup)
	}
	if data, _ := os.ReadFile(filepath.Join(root, "new.txt")); string(data) != "mine" {
		t.Errorf("new.txt: %q", data)
	}
	if list, _ := git(root, "stash", "list", "--format=%H"); !strings.Contains(list, strings.TrimSpace(theirs)) || strings.Count(list, "\n") != 2 {
		t.Errorf("stashes: %s", list)
	}
}
