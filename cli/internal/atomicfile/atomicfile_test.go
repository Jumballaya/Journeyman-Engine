package atomicfile

import (
	"errors"
	"io"
	"os"
	"path/filepath"
	"testing"
)

func TestWriteFileReplacesTheFileAndLeavesNoTemporary(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "level.scene.json")
	if err := os.WriteFile(path, []byte("old"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := WriteFile(path, []byte("new"), 0o644); err != nil {
		t.Fatal(err)
	}
	if data, _ := os.ReadFile(path); string(data) != "new" {
		t.Fatalf("got %q, want new", data)
	}
	if entries, _ := os.ReadDir(dir); len(entries) != 1 {
		t.Fatalf("want only the file, got %d entries", len(entries))
	}
}

func TestAFailedWriteKeepsTheOldFileWhole(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "level.scene.json")
	if err := os.WriteFile(path, []byte(`{"entities": []}`), 0o644); err != nil {
		t.Fatal(err)
	}
	crash := errors.New("crashed mid-write")
	err := Write(path, 0o644, func(w io.Writer) error {
		_, _ = w.Write([]byte(`{"ent`))
		return crash
	})
	if !errors.Is(err, crash) {
		t.Fatalf("got %v, want the fill's error", err)
	}
	if data, _ := os.ReadFile(path); string(data) != `{"entities": []}` {
		t.Fatalf("old file changed to %q", data)
	}
	if entries, _ := os.ReadDir(dir); len(entries) != 1 {
		t.Fatalf("temporary left behind: %d entries", len(entries))
	}
}

func TestWriteFileSetsTheMode(t *testing.T) {
	path := filepath.Join(t.TempDir(), "jm")
	if err := WriteFile(path, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	if info, _ := os.Stat(path); info.Mode().Perm() != 0o644 {
		t.Fatalf("mode %v, want 0644", info.Mode().Perm())
	}
}

func TestWriteFileKeepsAnExistingFilesModeAndSymlink(t *testing.T) {
	dir := t.TempDir()
	shared := filepath.Join(dir, "shared.json")
	if err := os.WriteFile(shared, []byte("old"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(shared, 0o600); err != nil {
		t.Fatal(err)
	}
	link := filepath.Join(dir, "level.scene.json")
	if err := os.Symlink(shared, link); err != nil {
		t.Skip("no symlinks here:", err)
	}
	if err := WriteFile(link, []byte("new"), 0o644); err != nil {
		t.Fatal(err)
	}
	if info, _ := os.Lstat(link); info.Mode()&os.ModeSymlink == 0 {
		t.Fatal("the symlink was replaced by a file")
	}
	if data, _ := os.ReadFile(shared); string(data) != "new" {
		t.Fatalf("target holds %q, want new", data)
	}
	if info, _ := os.Stat(shared); info.Mode().Perm() != 0o600 {
		t.Fatalf("mode %v, want 0600 kept", info.Mode().Perm())
	}
}
