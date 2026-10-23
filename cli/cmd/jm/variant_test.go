package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/copies"
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
	// The game moving on meanwhile isn't the variant's doing.
	os.WriteFile(filepath.Join(game, "later.md"), []byte("x"), 0o644)
	if _, err := vcs.Commit(game, "Later"); err != nil {
		t.Fatal(err)
	}
	cmp, err := compareVariants(nil, game, "floaty", "game", "none", "60", false)
	if err != nil || jsonOf(cmp.Changes) != `[{"path":"scenes/main.scene.json","status":"M","entities":[{"entity":"Bat","change":"added"}]}]` || cmp.Errors[0] == "" {
		t.Errorf("compare: %v %+v", err, cmp)
	}
	// The game's frame runs its uncommitted work too: the comparison says so.
	os.WriteFile(filepath.Join(game, "draft.md"), []byte("x"), 0o644)
	if cmp, _ := compareVariants(nil, game, "floaty", "game", "none", "60", false); cmp.Uncommitted != [2]int{0, 1} || !strings.Contains(cmp.text(), "game has 1 uncommitted files") {
		t.Errorf("uncommitted: %v\n%s", cmp.Uncommitted, cmp.text())
	}
	os.Remove(filepath.Join(game, "draft.md"))
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

// A merge that would replace an ignored file of the person's (or put a folder
// where it is) is refused: HEAD, the index and the file stay as they were.
func TestAMergeOverAnIgnoredFileChangesNothing(t *testing.T) {
	for _, add := range []string{"assets/personal.txt", "assets/personal.txt/in.txt", "assets/Personal.txt"} {
		if add == "assets/Personal.txt" && !caseFolds(t) {
			continue // a case-sensitive disk: Personal.txt is another file
		}
		game := variantGame(t)
		v, err := newVariant(game, "idea", "")
		if err != nil {
			t.Fatal(err)
		}
		os.MkdirAll(filepath.Dir(filepath.Join(v.Dir, add)), 0o755)
		os.WriteFile(filepath.Join(v.Dir, add), []byte("variant's"), 0o644)
		if _, err := vcs.Commit(v.Dir, "add it"); err != nil {
			t.Fatal(err)
		}
		os.WriteFile(filepath.Join(game, ".gitignore"), []byte(".jm/\nbuild/\nassets/personal.txt\n"), 0o644)
		os.WriteFile(filepath.Join(game, "assets/personal.txt"), []byte("mine"), 0o644)
		head, _ := exec.Command("git", "-C", game, "rev-parse", "HEAD").Output()
		index, _ := exec.Command("git", "-C", game, "ls-files", "-s").Output()
		if _, err := mergeVariant(game, "idea"); err == nil {
			t.Errorf("%s: merged over an ignored file", add)
		}
		after, _ := exec.Command("git", "-C", game, "rev-parse", "HEAD").Output()
		indexAfter, _ := exec.Command("git", "-C", game, "ls-files", "-s").Output()
		if data, _ := os.ReadFile(filepath.Join(game, "assets/personal.txt")); string(data) != "mine" || string(after) != string(head) || string(indexAfter) != string(index) {
			t.Errorf("%s: changed: %q, HEAD %s -> %s", add, data, head, after)
		}
	}
}

// A version whose .jm is a link, a file, or has a link or folder for its
// .gitignore makes no variant, writes nowhere, and leaves nothing behind.
func TestAVersionWithAnOddJMMakesNoVariant(t *testing.T) {
	outside := t.TempDir()
	os.WriteFile(filepath.Join(outside, ".gitignore"), []byte("personal\n"), 0o644)
	os.WriteFile(filepath.Join(outside, "ignore-file"), []byte("personal\n"), 0o644)
	shapes := map[string]func(game string){
		"a link .jm": func(g string) { os.Symlink(outside, filepath.Join(g, ".jm")) },
		"a file .jm": func(g string) { os.WriteFile(filepath.Join(g, ".jm"), []byte("x"), 0o644) },
		"a link .jm/.gitignore": func(g string) {
			os.MkdirAll(filepath.Join(g, ".jm"), 0o755)
			os.Symlink(filepath.Join(outside, "ignore-file"), filepath.Join(g, ".jm", ".gitignore"))
		},
		"a folder .jm/.gitignore": func(g string) {
			os.MkdirAll(filepath.Join(g, ".jm", ".gitignore"), 0o755)
			os.WriteFile(filepath.Join(g, ".jm", ".gitignore", "x"), []byte("x"), 0o644)
		},
	}
	for shape, make := range shapes {
		game := variantGame(t)
		os.WriteFile(filepath.Join(game, ".gitignore"), []byte("build/\n"), 0o644)
		os.RemoveAll(filepath.Join(game, ".jm"))
		make(game)
		exec.Command("git", "-C", game, "add", "-f", "-A").Run()
		old, err := vcs.Commit(game, "odd .jm")
		if err != nil {
			t.Fatalf("%s: %v", shape, err)
		}
		exec.Command("git", "-C", game, "rm", "-r", "-q", "--cached", ".jm").Run()
		os.RemoveAll(filepath.Join(game, ".jm"))
		os.WriteFile(filepath.Join(game, ".gitignore"), []byte(".jm/\nbuild/\n"), 0o644)
		if _, err := vcs.Commit(game, "plain again"); err != nil {
			t.Fatalf("%s: %v", shape, err)
		}
		if _, err := newVariant(game, "odd", old.Hash); err == nil {
			t.Errorf("%s: a variant made", shape)
		}
		for _, f := range []string{".gitignore", "ignore-file"} {
			if data, _ := os.ReadFile(filepath.Join(outside, f)); string(data) != "personal\n" {
				t.Errorf("%s: %s outside was written: %q", shape, f, data)
			}
		}
		branches, _ := exec.Command("git", "-C", game, "branch", "--list", variantPrefix+"odd").Output()
		worktrees, _ := exec.Command("git", "-C", game, "worktree", "list").Output()
		if len(branches) != 0 || strings.Count(string(worktrees), "\n") != 1 || exists(filepath.Join(copies.Store(game, copies.Variant), "odd")) {
			t.Errorf("%s: left behind: %q %q", shape, branches, worktrees)
		}
		if _, err := newVariant(game, "odd", ""); err != nil {
			t.Errorf("%s: trying again: %v", shape, err)
		}
	}
}

func caseFolds(t *testing.T) bool {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "a"), nil, 0o644)
	return exists(filepath.Join(dir, "A"))
}

// A file the game stopped tracking and now keeps ignored doesn't hold up a
// variant that left it alone: the merge goes through, the local file stays.
func TestAMergeLeavesAnIgnoredFileItDoesntTouch(t *testing.T) {
	game := variantGame(t)
	os.WriteFile(filepath.Join(game, "notes.txt"), []byte("tracked"), 0o644)
	vcs.Commit(game, "notes")
	v, err := newVariant(game, "idea", "")
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(v.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Bat","components":{}}]}`), 0o644)
	vcs.Commit(v.Dir, "a bat")
	exec.Command("git", "-C", game, "rm", "-q", "--cached", "notes.txt").Run()
	os.WriteFile(filepath.Join(game, ".gitignore"), []byte(".jm/\nbuild/\nnotes.txt\n"), 0o644)
	if _, err := vcs.Commit(game, "notes are mine now"); err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(game, "notes.txt"), []byte("mine"), 0o644)
	if _, err := mergeVariant(game, "idea"); err != nil {
		t.Fatalf("merge: %v", err)
	}
	if data, _ := os.ReadFile(filepath.Join(game, "notes.txt")); string(data) != "mine" {
		t.Errorf("notes.txt: %q", data)
	}
}

// A fresh variant is the game: with no picture of its own, it shows the game's.
func TestAFreshVariantShowsTheGamesPicture(t *testing.T) {
	game := variantGame(t)
	if _, err := newVariant(game, "fresh", ""); err != nil {
		t.Fatal(err)
	}
	sb, err := newSandbox(game, "try", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	golden := filepath.Join(game, "tests", "golden", "start", "frame_1.png") // made after them: only the game has it
	os.MkdirAll(filepath.Dir(golden), 0o755)
	os.WriteFile(golden, []byte("png"), 0o644)
	if list := variantsOf(game); len(list) != 1 || list[0].thumb != golden {
		t.Errorf("variants: %+v", list)
	}
	if list, _ := sandboxesOf(game); len(list) != 1 || list[0].thumb != golden {
		t.Errorf("sandboxes: %+v", list)
	}
	// One with a picture of its own shows that.
	own := filepath.Join(sb.Dir, "tests", "golden", "mine", "frame_1.png")
	os.MkdirAll(filepath.Dir(own), 0o755)
	os.WriteFile(own, []byte("png"), 0o644)
	if list, _ := sandboxesOf(game); len(list) != 1 || list[0].thumb != own {
		t.Errorf("sandbox's own: %+v", list)
	}
}

// A variant of a game deep in its repository builds with the library beside
// the game as it is now, not the worktree's committed copy of it.
func TestAVariantBuildsWithItsGamesLibrary(t *testing.T) {
	if _, err := exec.LookPath("git"); err != nil {
		t.Skip("no git")
	}
	t.Setenv("GIT_CONFIG_GLOBAL", os.DevNull)
	t.Setenv("GIT_CONFIG_NOSYSTEM", "1")
	repo := t.TempDir()
	game := filesGame(t, filepath.Join(repo, "games", "pip"))
	shared := filepath.Join(repo, "shared")
	os.MkdirAll(shared, 0o755)
	os.WriteFile(filepath.Join(shared, "index.ts"), []byte("// committed\n"), 0o644)
	os.WriteFile(filepath.Join(repo, ".gitignore"), []byte(".jm/\nbuild/\n"), 0o644)
	os.WriteFile(filepath.Join(game, ".jm.json"), []byte(`{"name": "Pip", "scriptLibraries": {"shared": "../../shared"}}`), 0o644)
	git(t, repo, "init", "-q")
	git(t, repo, "add", "-A")
	git(t, repo, "commit", "-q", "-m", "start")
	os.WriteFile(filepath.Join(shared, "index.ts"), []byte("// now\n"), 0o644)
	v, err := newVariant(game, "try", "")
	if err != nil {
		t.Fatal(err)
	}
	if err := syncLibrary(v.Dir, "shared", "../../shared"); err != nil {
		t.Fatal(err)
	}
	if got, _ := os.ReadFile(scriptsPath(v.Dir, "node_modules", "shared", "index.ts")); string(got) != "// now\n" {
		t.Errorf("the variant's library is %q, want the game's (// now)", got)
	}
	if _, stamped := buildSources(v.Dir)[shared]; !stamped {
		t.Errorf("the variant's build doesn't watch the game's library %s", shared)
	}
}

// The agent's compare opens the Studio on it: the Studio's answer, the comparison in it.
func TestTheAgentsCompareShowsInTheStudio(t *testing.T) {
	game := variantGame(t)
	if _, err := newVariant(game, "floaty", ""); err != nil {
		t.Fatal(err)
	}
	t.Setenv("JM_ENGINE", filepath.Join(t.TempDir(), "none")) // no frames: the comparison still answers
	tool := toolOf(t, newMCPServer(), "variant_compare")
	if ui, _ := tool.Meta["ui"].(map[string]any); ui == nil || ui["resourceUri"] != studio.appURI() {
		t.Errorf("not shown in the Studio: %v", tool.Meta)
	}
	r := tool.run(toolArgs{"game": game, "a": "floaty", "play": "none", "at": "60"})
	sc, _ := r.Structured.(map[string]any)
	compared, _ := sc["compared"].(map[string]any)
	if r.Failed || sc["games"] == nil || compared == nil || compared["a"] != "floaty" || compared["game"] != evalOrSelf(game) || compared["at"] != "60" || r.Meta["jm/frames"] == nil {
		t.Errorf("the Studio's answer with the comparison: %+v", r)
	}
}
