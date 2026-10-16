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
	if _, err := runInSandbox(game, "sb1", []string{"mcp"}, time.Second); err == nil {
		t.Error("ran jm mcp in a sandbox")
	}
}

func TestExpiredSandboxesGo(t *testing.T) {
	game := variantGame(t)
	sb, _ := newSandbox(game, "old", time.Hour)
	data, _ := json.Marshal(sandboxMeta{Made: 1, Expires: time.Now().Unix() - 1})
	os.WriteFile(filepath.Join(sandboxesDir(game), "old.json"), data, 0o644)
	if list := sandboxesOf(game); len(list) != 0 || exists(sb.Dir) {
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
		if _, err := runInSandbox(game, "s", args, time.Second); err == nil || !strings.Contains(err.Error(), "leads out") {
			t.Errorf("%v: %v", args, err)
		}
	}
}
