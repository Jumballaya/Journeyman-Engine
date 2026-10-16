package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"
)

// variantGame is a committed game with no engine (frames can't be made).
func variantGame(t *testing.T) string {
	t.Helper()
	if _, err := exec.LookPath("git"); err != nil {
		t.Skip("no git")
	}
	t.Setenv("GIT_CONFIG_GLOBAL", os.DevNull)
	t.Setenv("GIT_CONFIG_NOSYSTEM", "1")
	game := filesGame(t, t.TempDir())
	os.WriteFile(filepath.Join(game, ".gitignore"), []byte(".jm/\nbuild/\n"), 0o644)
	if _, err := vcs.Commit(game, "start"); err != nil {
		t.Fatal(err)
	}
	return game
}

func TestAVariantIsTriedComparedAndMerged(t *testing.T) {
	game := variantGame(t)
	if _, err := newVariant(game, "Floaty Jump", ""); err == nil {
		t.Error("a name with spaces")
	}
	v, err := newVariant(game, "floaty", "")
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(v.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Bat","components":{}}]}`), 0o644)
	if list := variantsOf(game); len(list) != 1 || list[0].Changed != 1 || list[0].Ahead != 0 {
		t.Errorf("variants: %+v", list)
	}
	if got := vcs.Status(game); len(got) != 0 {
		t.Errorf("the game changed: %v", got)
	}
	if _, err := vcs.Commit(v.Dir, "Add a bat"); err != nil {
		t.Fatal(err)
	}
	cmp, err := compareVariants(game, "floaty", "game", 60, "")
	if err != nil || jsonOf(cmp.Changes) != `[{"path":"scenes/main.scene.json","status":"M","entities":[{"entity":"Bat","change":"added"}]}]` || cmp.Errors[0] == "" {
		t.Errorf("compare: %v %+v", err, cmp)
	}
	m, err := mergeVariant(game, "floaty")
	if err != nil || m.Subject != "Merge variant floaty" {
		t.Fatalf("merge: %+v %v", m, err)
	}
	if data, _ := os.ReadFile(filepath.Join(game, "scenes/main.scene.json")); !strings.Contains(string(data), "Bat") {
		t.Errorf("the game didn't get the bat: %s", data)
	}
	if kept, err := dropVariant(game, "floaty"); err != nil || kept || exists(v.Dir) {
		t.Errorf("drop a merged variant: kept %v, %v", kept, err)
	}
}

func TestAnUnmergedVariantsBranchOutlivesIt(t *testing.T) {
	game := variantGame(t)
	v, _ := newVariant(game, "idea", "")
	os.WriteFile(filepath.Join(v.Dir, "notes.md"), []byte("x"), 0o644)
	vcs.Commit(v.Dir, "idea")
	if kept, err := dropVariant(game, "idea"); err != nil || !kept {
		t.Errorf("drop: kept %v, %v", kept, err)
	}
	if len(variantsOf(game)) != 0 {
		t.Error("still listed")
	}
}

func TestAConflictingMergeChangesNothing(t *testing.T) {
	game := variantGame(t)
	v, _ := newVariant(game, "a", "")
	os.WriteFile(filepath.Join(v.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"A"}]}`), 0o644)
	os.WriteFile(filepath.Join(game, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"B"}]}`), 0o644)
	vcs.Commit(game, "B")
	if _, err := mergeVariant(game, "a"); err == nil || !strings.Contains(err.Error(), "scenes/main.scene.json") {
		t.Fatalf("merge: %v", err)
	}
	if data, _ := os.ReadFile(filepath.Join(game, "scenes/main.scene.json")); string(data) != `{"entities":[{"name":"B"}]}` || len(vcs.Status(game)) != 0 {
		t.Errorf("the game after a conflict: %s %v", data, vcs.Status(game))
	}
}

func TestAGameThatDoesntIgnoreJMGetsItIgnored(t *testing.T) {
	game := variantGame(t)
	os.WriteFile(filepath.Join(game, ".gitignore"), []byte("build/\n"), 0o644)
	vcs.Commit(game, "no .jm ignore")
	if _, err := newVariant(game, "x", ""); err != nil {
		t.Fatal(err)
	}
	if got := vcs.Status(game); len(got) != 1 || got[".gitignore"] != "M" {
		t.Errorf("status: %v", got)
	}
}

// A nested game's variant that changed the repository outside the game isn't merged: the game's merge is the game's.
func TestAVariantChangingOutsideTheGameIsntMerged(t *testing.T) {
	if _, err := exec.LookPath("git"); err != nil {
		t.Skip("no git")
	}
	t.Setenv("GIT_CONFIG_GLOBAL", os.DevNull)
	t.Setenv("GIT_CONFIG_NOSYSTEM", "1")
	repo := t.TempDir()
	game := filesGame(t, filepath.Join(repo, "games", "pip"))
	os.MkdirAll(filepath.Join(repo, "docs"), 0o755)
	os.WriteFile(filepath.Join(repo, "docs", "a.md"), []byte("docs"), 0o644)
	os.WriteFile(filepath.Join(repo, ".gitignore"), []byte(".jm/\nbuild/\n"), 0o644)
	gitIn := func(dir string, args ...string) {
		cmd := exec.Command("git", append([]string{"-C", dir, "-c", "user.name=t", "-c", "user.email=t@t"}, args...)...)
		if out, err := cmd.CombinedOutput(); err != nil {
			t.Fatalf("git %v: %v %s", args, err, out)
		}
	}
	gitIn(repo, "init", "-q")
	gitIn(repo, "add", "-A")
	gitIn(repo, "commit", "-q", "-m", "start")
	v, err := newVariant(game, "wide", "")
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(v.Dir, "..", "..", "docs", "a.md"), []byte("changed by the variant"), 0o644)
	os.WriteFile(filepath.Join(v.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Wide","components":{}}]}`), 0o644)
	gitIn(v.Dir, "add", "-A", "--", "../../docs", ".")
	gitIn(v.Dir, "commit", "-q", "-m", "docs and game")
	head, _ := exec.Command("git", "-C", repo, "rev-parse", "HEAD").Output()
	if _, err := mergeVariant(game, "wide"); err == nil {
		t.Error("merged a change outside the game")
	}
	after, _ := exec.Command("git", "-C", repo, "rev-parse", "HEAD").Output()
	if data, _ := os.ReadFile(filepath.Join(repo, "docs", "a.md")); string(data) != "docs" || string(after) != string(head) {
		t.Errorf("the repository changed: docs %q", data)
	}
	ok, err := newVariant(game, "tall", "")
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(ok.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Tall","components":{}}]}`), 0o644)
	if _, err := mergeVariant(game, "tall"); err != nil {
		t.Errorf("a game-only variant: %v", err)
	}
}

// git's merge.autoStash doesn't let a merge write conflict markers over the game's own edit.
func TestAMergeOverTheGamesEditsWithAutoStashChangesNothing(t *testing.T) {
	game := variantGame(t)
	exec.Command("git", "-C", game, "config", "merge.autoStash", "true").Run()
	v, _ := newVariant(game, "other", "")
	os.WriteFile(filepath.Join(v.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Theirs","components":{}}]}`), 0o644)
	vcs.Commit(v.Dir, "theirs")
	mine := `{"entities":[{"name":"Mine","components":{}}]}`
	os.WriteFile(filepath.Join(game, "scenes/main.scene.json"), []byte(mine), 0o644)
	if _, err := mergeVariant(game, "other"); err == nil {
		t.Error("merged over the game's edit")
	}
	if data, _ := os.ReadFile(filepath.Join(game, "scenes/main.scene.json")); string(data) != mine {
		t.Errorf("the game's edit: %s", data)
	}
}

// A variant from a commit that didn't ignore .jm still keeps .jm out of its commits; one from before the game existed isn't made.
func TestAnOldVariantKeepsJMOutAndAGamelessOneIsntMade(t *testing.T) {
	game := variantGame(t)
	os.WriteFile(filepath.Join(game, ".gitignore"), []byte("build/\n"), 0o644)
	old, _ := vcs.Commit(game, "forgets .jm")
	os.WriteFile(filepath.Join(game, ".gitignore"), []byte(".jm/\nbuild/\n"), 0o644)
	vcs.Commit(game, "ignores .jm")
	v, err := newVariant(game, "older", old.Hash)
	if err != nil {
		t.Fatal(err)
	}
	os.MkdirAll(filepath.Join(v.Dir, ".jm", "plays"), 0o755)
	os.WriteFile(filepath.Join(v.Dir, ".jm", "plays", "x.txt"), []byte("a play"), 0o644)
	os.WriteFile(filepath.Join(v.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Old","components":{}}]}`), 0o644)
	if _, err := mergeVariant(game, "older"); err != nil {
		t.Fatal(err)
	}
	if out, _ := exec.Command("git", "-C", game, "ls-files", ".jm/plays/x.txt").Output(); len(out) != 0 || exists(filepath.Join(game, ".jm", "plays", "x.txt")) {
		t.Errorf(".jm was committed: %s", out)
	}
	// A commit before the game: no game there to vary.
	repo := t.TempDir()
	exec.Command("git", "-C", repo, "init", "-q").Run()
	os.WriteFile(filepath.Join(repo, "README"), []byte("x"), 0o644)
	exec.Command("git", "-C", repo, "add", "-A").Run()
	exec.Command("git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "before").Run()
	first, _ := exec.Command("git", "-C", repo, "rev-parse", "HEAD").Output()
	pip := filesGame(t, filepath.Join(repo, "games", "pip"))
	vcs.Commit(pip, "pip")
	if _, err := newVariant(pip, "early", strings.TrimSpace(string(first))); err == nil {
		t.Error("a variant from before the game")
	}
	if out, _ := exec.Command("git", "-C", repo, "worktree", "list").Output(); strings.Count(string(out), "\n") != 1 {
		t.Errorf("a worktree was left: %s", out)
	}
	if _, err := newVariant(pip, "early", ""); err != nil {
		t.Errorf("trying again: %v", err)
	}
}
