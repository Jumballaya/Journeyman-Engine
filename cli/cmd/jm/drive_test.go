package main

import (
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// A game that ends before saying it's ready is an error, not a crash, and
// leaves nothing behind: no engine, no temp folder, no empty play.
func TestAGameThatDoesntStartLeavesNothing(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("uses /usr/bin/true as the engine")
	}
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, "build"), 0o755)
	os.WriteFile(filepath.Join(root, "build", ".jm.json"), []byte(`{}`), 0o644)
	t.Setenv("JM_ENGINE", "/usr/bin/true")
	t.Setenv("TMPDIR", t.TempDir())
	if g, err := startGame(root, gameOptions{Record: true}); err == nil || g != nil {
		t.Fatalf("started: %v, %v", g, err)
	}
	if leftover, _ := filepath.Glob(filepath.Join(os.Getenv("TMPDIR"), "jm-drive-*")); len(leftover) > 0 {
		t.Errorf("left %v", leftover)
	}
	if all, _ := os.ReadDir(plays.Root(root)); len(all) > 0 {
		t.Errorf("left a play: %v", all[0].Name())
	}
}
