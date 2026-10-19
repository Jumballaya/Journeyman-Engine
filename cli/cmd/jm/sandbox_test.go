package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"
)

func TestASandboxIsACopyThatNeverTouchesTheGame(t *testing.T) {
	game := variantGame(t)
	os.WriteFile(filepath.Join(game, "notes.md"), []byte("uncommitted"), 0o644)
	sb, err := newSandbox(game, "", time.Hour)
	if err != nil || sb.Name != "sb1" || len(sb.Changes) != 0 {
		t.Fatalf("new: %+v %v", sb, err)
	}
	if data, _ := os.ReadFile(filepath.Join(sb.Dir, "notes.md")); string(data) != "uncommitted" {
		t.Error("the game's uncommitted work isn't in the copy")
	}
	if exists(filepath.Join(sb.Dir, "build")) || exists(filepath.Join(sb.Dir, ".jm")) {
		t.Error("build output or .jm was copied")
	}
	os.WriteFile(filepath.Join(sb.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"X"}]}`), 0o644)
	os.WriteFile(filepath.Join(sb.Dir, "new.ts"), []byte("x"), 0o644)
	os.Remove(filepath.Join(sb.Dir, "notes.md"))
	got, _ := findSandbox(game, "sb1")
	if jsonOf(got.Changes) != `{"new.ts":"A","notes.md":"D","scenes/main.scene.json":"M"}` {
		t.Errorf("changes: %v", got.Changes)
	}
	if data, _ := os.ReadFile(filepath.Join(game, "scenes/main.scene.json")); strings.Contains(string(data), "X") {
		t.Error("the game changed")
	}
	if again, _ := newSandbox(game, "", time.Hour); again.Name != "sb2" {
		t.Errorf("second: %s", again.Name)
	}
	if _, err := runInSandbox(nil, game, "sb1", []string{"mcp"}, time.Second); err == nil {
		t.Error("ran jm mcp in a sandbox")
	}
}

func TestExpiredSandboxesGo(t *testing.T) {
	game := variantGame(t)
	sb, _ := newSandbox(game, "old", time.Hour)
	data, _ := json.Marshal(sandboxMeta{Made: 1, Expires: time.Now().Unix() - 1})
	os.WriteFile(filepath.Join(game, ".jm", "sandboxes", "old.json"), data, 0o644)
	if list, _ := sandboxesOf(game); len(list) != 0 || exists(sb.Dir) {
		t.Errorf("an expired sandbox stayed: %v", list)
	}
}

func TestASandboxThatWorkedBecomesAVariant(t *testing.T) {
	game := variantGame(t)
	sb, _ := newSandbox(game, "try", time.Hour)
	os.WriteFile(filepath.Join(sb.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Y"}]}`), 0o644)
	os.Remove(filepath.Join(sb.Dir, "assets/scripts/player.ts"))
	v, err := promoteSandbox(game, "try", "kept")
	if err != nil {
		t.Fatal(err)
	}
	if v.Ahead != 1 || v.Changed != 0 || exists(sb.Dir) {
		t.Errorf("variant: %+v (sandbox left: %v)", v, exists(sb.Dir))
	}
	if exists(filepath.Join(v.Dir, "assets/scripts/player.ts")) || vcs.Branch(v.Dir) != "variant/kept" {
		t.Error("the variant isn't the sandbox, or lost its worktree")
	}
}

func TestPromotingReplacesFilesFoldersAndLinksInTheWay(t *testing.T) {
	game := variantGame(t)
	outside := filepath.Join(t.TempDir(), "x.png")
	os.WriteFile(outside, []byte("theirs"), 0o644)
	os.Symlink(outside, filepath.Join(game, "art.png"))
	vcs.Commit(game, "a link")
	sb, _ := newSandbox(game, "swap", time.Hour)
	os.Remove(filepath.Join(sb.Dir, "assets/scripts/player.ts"))
	os.MkdirAll(filepath.Join(sb.Dir, "assets/scripts/player.ts"), 0o755)
	os.WriteFile(filepath.Join(sb.Dir, "assets/scripts/player.ts/a.ts"), []byte("a"), 0o644)
	os.WriteFile(filepath.Join(sb.Dir, "art.png"), []byte("mine"), 0o644)
	v, err := promoteSandbox(game, "swap", "swapped")
	if err != nil {
		t.Fatal(err)
	}
	if data, _ := os.ReadFile(filepath.Join(v.Dir, "assets/scripts/player.ts/a.ts")); string(data) != "a" {
		t.Error("a file replaced by a folder")
	}
	if data, _ := os.ReadFile(outside); string(data) != "theirs" {
		t.Errorf("wrote through the link: %s", data)
	}
	if data, _ := os.ReadFile(filepath.Join(v.Dir, "art.png")); string(data) != "mine" {
		t.Errorf("the link wasn't replaced: %s", data)
	}
}

func TestSandboxRunsStayInTheCopy(t *testing.T) {
	game := variantGame(t)
	if _, err := newSandbox(game, "s", time.Hour); err != nil {
		t.Fatal(err)
	}
	for _, args := range [][]string{{"fmt", "../../../.jm.json"}, {"fmt", "/etc/hosts"}, {"export", "--out=../x"}} {
		if _, err := runInSandbox(nil, game, "s", args, time.Second); err == nil || !strings.Contains(err.Error(), "leads out") {
			t.Errorf("%v: %v", args, err)
		}
	}
}

// A sandbox folder that can't be read isn't taken for deleted files: promoting refuses, nothing changed.
func TestAnUnreadableSandboxFolderIsntPromoted(t *testing.T) {
	game := variantGame(t)
	sb, _ := newSandbox(game, "dim", time.Hour)
	scripts := filepath.Join(sb.Dir, "assets", "scripts")
	os.Chmod(scripts, 0o000)
	defer os.Chmod(scripts, 0o755)
	if _, err := os.ReadDir(scripts); err == nil {
		t.Skip("the folder is readable anyway (root?)")
	}
	if _, err := promoteSandbox(game, "dim", "dim-kept"); err == nil {
		t.Error("promoted with a folder unread")
	}
	os.Chmod(scripts, 0o755)
	if !exists(filepath.Join(sb.Dir, "assets/scripts/player.ts")) || len(variantsOf(game)) != 0 {
		t.Errorf("the sandbox lost files, or a variant was left")
	}
	if _, err := promoteSandbox(game, "dim", "dim-kept"); err != nil {
		t.Errorf("readable again: %v", err)
	}
}

// A run's paths can't lead out through a link; a link at a sandbox's name isn't followed.
func TestSandboxLinksDontLeadOut(t *testing.T) {
	game := variantGame(t)
	sb, _ := newSandbox(game, "s", time.Hour)
	before, _ := os.ReadFile(filepath.Join(game, "scenes/main.scene.json"))
	if os.Symlink(filepath.Join(game, "scenes/main.scene.json"), filepath.Join(sb.Dir, "theirs.json")) != nil {
		t.Skip("no symlinks here")
	}
	os.Symlink(filepath.Join(game, "scenes"), filepath.Join(sb.Dir, "away"))
	os.Symlink(filepath.Join(game, "nothing.json"), filepath.Join(sb.Dir, "dangling.json"))
	for _, args := range [][]string{{"fmt", "theirs.json"}, {"fmt", "away/main.scene.json"}, {"fmt", "dangling.json"}, {"editor"}, {"variant", "play", "x"}} {
		if _, err := runInSandbox(nil, game, "s", args, time.Second); err == nil {
			t.Errorf("%v ran", args)
		}
	}
	if after, _ := os.ReadFile(filepath.Join(game, "scenes/main.scene.json")); string(after) != string(before) {
		t.Error("the game's scene changed")
	}
	outside := t.TempDir()
	os.WriteFile(filepath.Join(outside, "precious.txt"), []byte("mine"), 0o644)
	os.Symlink(outside, filepath.Join(filepath.Join(game, ".jm", "sandboxes"), "trap"))
	if _, err := newSandbox(game, "trap", time.Hour); err == nil {
		t.Error("made a sandbox through a link")
	}
	if entries, _ := os.ReadDir(outside); len(entries) != 1 {
		t.Errorf("wrote outside: %v", entries)
	}
}

// What a command finds on its own (fmt with no names) can't lead out either:
// a link out of the copy anywhere in it, or the copy swapped for a link, stops the run.
func TestASandboxRunWithALinkOutDoesNothing(t *testing.T) {
	game := variantGame(t)
	sb, _ := newSandbox(game, "s", time.Hour)
	scene := filepath.Join(game, "scenes/main.scene.json")
	before, _ := os.ReadFile(scene)
	os.WriteFile(scene, append([]byte("  "), before...), 0o644) // fmt would rewrite it
	before, _ = os.ReadFile(scene)
	if os.Symlink(scene, filepath.Join(sb.Dir, "assets", "probe.json")) != nil {
		t.Skip("no symlinks here")
	}
	os.Remove(filepath.Join(sb.Dir, "assets", "probe.json"))
	for _, at := range []string{"assets/escape.json", "scenes/escape.scene.json", ".jm.json"} {
		link := filepath.Join(sb.Dir, filepath.FromSlash(at))
		kept, _ := os.ReadFile(link)
		os.Remove(link)
		os.Symlink(scene, link)
		if _, err := runInSandbox(nil, game, "s", []string{"fmt"}, 5*time.Second); err == nil || !strings.Contains(err.Error(), "link out") {
			t.Errorf("%s: %v", at, err)
		}
		os.Remove(link)
		if kept != nil {
			os.WriteFile(link, kept, 0o644)
		}
	}
	if after, _ := os.ReadFile(scene); string(after) != string(before) {
		t.Error("the game's scene changed")
	}
	os.Symlink(filepath.Join(sb.Dir, "scenes/main.scene.json"), filepath.Join(sb.Dir, "assets", "inner.json"))
	if err := linksStayIn(game, sb.Dir); err != nil { // a run here would run the test binary
		t.Errorf("a link inside the copy: %v", err)
	}
	outside := filesGame(t, t.TempDir())
	os.WriteFile(filepath.Join(outside, "local.json"), []byte(" {} "), 0o644)
	os.RemoveAll(sb.Dir)
	os.Symlink(outside, sb.Dir) // the metadata stays
	if _, err := runInSandbox(nil, game, "s", []string{"fmt", "local.json"}, 5*time.Second); err == nil {
		t.Error("ran in a sandbox swapped for a link")
	}
	if data, _ := os.ReadFile(filepath.Join(outside, "local.json")); string(data) != " {} " {
		t.Errorf("the outside game changed: %q", data)
	}
}

// Flags anywhere don't hide a command that outlives the run, or a sandbox in a sandbox.
func TestASandboxRunKnowsTheCommandWhereverTheFlagsAre(t *testing.T) {
	game := variantGame(t)
	newSandbox(game, "s", time.Hour)
	for _, args := range [][]string{{"--help=false", "editor"}, {"variant", "--json", "play", "v"}, {"--help=false", "sandbox", "list"}, {"--help=false", "mcp"}} {
		if _, err := runInSandbox(nil, game, "s", args, time.Second); err == nil || strings.Contains(err.Error(), "ran out of time") {
			t.Errorf("%v: %v", args, err)
		}
	}
	for _, tool := range newMCPServer().tools {
		if tool.Name == "sandboxes" && (tool.Annotations["readOnlyHint"] != false || tool.Annotations["destructiveHint"] != true || tool.InputSchema == nil) {
			t.Errorf("sandboxes drops expired copies, yet: %v", tool.Annotations)
		}
	}
}

// A sandboxes folder (or .jm) linked out of the game isn't read, pruned or written.
func TestALinkedSandboxStoreIsLeftAlone(t *testing.T) {
	for _, linked := range []string{".jm/sandboxes", ".jm"} {
		game, outside := variantGame(t), t.TempDir()
		target := filepath.Join(outside, filepath.Base(linked)) // the link's folder, outside
		store := filepath.Join(outside, ".jm", "sandboxes")
		if linked == ".jm/sandboxes" {
			store = target
		}
		os.MkdirAll(filepath.Join(store, "old"), 0o755)
		os.WriteFile(filepath.Join(store, "old", "precious.txt"), []byte("mine"), 0o644)
		data, _ := json.Marshal(sandboxMeta{Made: 1, Expires: 1})
		os.WriteFile(filepath.Join(store, "old.json"), data, 0o644)
		os.MkdirAll(filepath.Join(game, ".jm"), 0o755)
		os.RemoveAll(filepath.Join(game, linked))
		os.Symlink(target, filepath.Join(game, linked))
		if _, err := sandboxesOf(game); err == nil {
			t.Errorf("%s: listed through the link", linked)
		}
		if _, err := runInSandbox(nil, game, "old", []string{"fmt"}, time.Second); err == nil {
			t.Errorf("%s: ran through the link", linked)
		}
		if _, err := newSandbox(game, "new", time.Hour); err == nil {
			t.Errorf("%s: made one through the link", linked)
		}
		if err := dropSandbox(game, "old"); err == nil {
			t.Errorf("%s: dropped through the link", linked)
		}
		if entries, _ := os.ReadDir(store); len(entries) != 2 || !exists(filepath.Join(store, "old", "precious.txt")) {
			t.Errorf("%s: changed outside: %v", linked, entries)
		}
	}
}

func TestASandboxIsComparedWithTheGameAsItIsNow(t *testing.T) {
	game := variantGame(t)
	os.WriteFile(filepath.Join(game, "notes.md"), []byte("uncommitted"), 0o644) // the copy has it too: no difference
	sb, err := newSandbox(game, "game", time.Hour)                              // named like the game, and still the sandbox
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(sb.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Bat","components":{}}]}`), 0o644)
	cmp, err := compareVariants(nil, game, "game", "game", 60, "", true)
	if err != nil || !cmp.Sandbox || jsonOf(cmp.Changes) != `[{"path":"scenes/main.scene.json","status":"M","entities":[{"entity":"Bat","change":"added"}]}]` {
		t.Errorf("compare: %v %+v", err, cmp)
	}
	if _, err := compareVariants(nil, game, "game", "other", 60, "", true); err == nil {
		t.Error("a sandbox compared with a variant")
	}
	red, err := newSandbox(game, "red", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(red.Dir, "scenes/main.scene.json"), []byte(`{"entities":[{"name":"Bat","components":{}}]}`), 0o644)
	if _, err := newVariant(game, "red", ""); err != nil { // the same name as the sandbox: each is still itself
		t.Fatal(err)
	}
	if cmp, err := compareVariants(nil, game, "red", "game", 60, "", true); err != nil || len(cmp.Changes) != 1 {
		t.Errorf("the sandbox beside a same-named variant: %v %+v", err, cmp)
	}
	if cmp, err := compareVariants(nil, game, "red", "game", 60, "", false); err != nil || cmp.Sandbox || len(cmp.Changes) != 0 {
		t.Errorf("the variant beside a same-named sandbox: %v %+v", err, cmp)
	}
}
