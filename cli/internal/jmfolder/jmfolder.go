// Package jmfolder is a game's .jm/: what jm and the engine keep beside the
// game (plays, variants, locks), never the game's own files.
package jmfolder

import (
	"os"
	"path/filepath"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
)

// Make makes the folder elem names in the game at root's .jm/ and gives its
// path. .jm/ ignores itself: nothing in it gets committed, even in a game
// whose .gitignore predates it.
func Make(root string, elem ...string) (string, error) {
	dir := filepath.Join(append([]string{root, ".jm"}, elem...)...)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	ignore := filepath.Join(root, ".jm", ".gitignore")
	if _, err := os.Stat(ignore); os.IsNotExist(err) {
		if err := atomicfile.WriteFile(ignore, []byte("*\n"), 0o644); err != nil {
			return "", err
		}
	}
	return dir, nil
}
