package jmfolder

import (
	"os"
	"path/filepath"
	"testing"
)

func TestTheFolderIgnoresItself(t *testing.T) {
	root := t.TempDir()
	dir, err := Make(root, "plays")
	if err != nil || dir != filepath.Join(root, ".jm", "plays") {
		t.Fatalf("Make = %q, %v", dir, err)
	}
	if data, err := os.ReadFile(filepath.Join(root, ".jm", ".gitignore")); err != nil || string(data) != "*\n" {
		t.Errorf(".jm/.gitignore = %q, %v", data, err)
	}
	if dir, err := Make(root); err != nil || dir != filepath.Join(root, ".jm") {
		t.Errorf("Make(root) = %q, %v", dir, err)
	}
}
