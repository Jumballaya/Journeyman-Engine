package atomicfile

import (
	"errors"
	"fmt"
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

// Modes are compared with os.WriteFile's, not literals: Windows reports 0666.
func TestWriteFileSetsTheModeLikeOsWriteFile(t *testing.T) {
	dir := t.TempDir()
	path, plain := filepath.Join(dir, "jm"), filepath.Join(dir, "plain")
	if err := WriteFile(path, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(plain, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	got, _ := os.Stat(path)
	want, _ := os.Stat(plain)
	if got.Mode().Perm() != want.Mode().Perm() {
		t.Fatalf("mode %v, want %v", got.Mode().Perm(), want.Mode().Perm())
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
	before, _ := os.Stat(shared)
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
	if info, _ := os.Stat(shared); info.Mode().Perm() != before.Mode().Perm() {
		t.Fatalf("mode %v, want %v kept", info.Mode().Perm(), before.Mode().Perm())
	}
}

func TestWriteFileCreatesADanglingSymlinksTarget(t *testing.T) {
	dir := t.TempDir()
	link := filepath.Join(dir, "level.scene.json")
	if err := os.Symlink("shared.json", link); err != nil {
		t.Skip("no symlinks here:", err)
	}
	if err := WriteFile(link, []byte("new"), 0o644); err != nil {
		t.Fatal(err)
	}
	if info, _ := os.Lstat(link); info.Mode()&os.ModeSymlink == 0 {
		t.Fatal("the symlink was replaced by a file")
	}
	if data, _ := os.ReadFile(filepath.Join(dir, "shared.json")); string(data) != "new" {
		t.Fatalf("target holds %q, want new", data)
	}
}

// The write must land where reading through link does: POSIX resolves alias
// before "..", Windows drops "alias\.." first, so either file may be it.
func checkWritesWhereReads(t *testing.T, dir, link string) {
	t.Helper()
	files := []string{filepath.Join(dir, "real", "shared.json"), filepath.Join(dir, "shared.json")}
	for _, f := range files {
		if err := os.WriteFile(f, []byte("old"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	if err := WriteFile(link, []byte("new"), 0o644); err != nil {
		t.Fatal(err)
	}
	if data, _ := os.ReadFile(link); string(data) != "new" {
		t.Fatalf("reading the link gives %q, want new", data)
	}
	a, _ := os.ReadFile(files[0])
	b, _ := os.ReadFile(files[1])
	if string(a)+string(b) != "newold" && string(a)+string(b) != "oldnew" {
		t.Fatalf("files hold %q and %q, want one new and one old", a, b)
	}
}

// linkedFolder makes dir/real/nested and the symlink dir/alias to it.
func linkedFolder(t *testing.T) (dir, nested string) {
	dir = t.TempDir()
	nested = filepath.Join(dir, "real", "nested")
	if err := os.MkdirAll(nested, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(nested, filepath.Join(dir, "alias")); err != nil {
		t.Skip("no symlinks here:", err)
	}
	return dir, nested
}

func TestARelativeSymlinkInALinkedFolderIsWrittenWhereItReads(t *testing.T) {
	dir, nested := linkedFolder(t)
	if err := os.Symlink(filepath.Join("..", "shared.json"), filepath.Join(nested, "scene.json")); err != nil {
		t.Fatal(err)
	}
	checkWritesWhereReads(t, dir, filepath.Join(dir, "alias", "scene.json"))
}

func TestASymlinkThroughALinkedFoldersParentIsWrittenWhereItReads(t *testing.T) {
	dir, _ := linkedFolder(t)
	link := filepath.Join(dir, "scene.json")
	if err := os.Symlink("alias"+string(filepath.Separator)+".."+string(filepath.Separator)+"shared.json", link); err != nil {
		t.Fatal(err)
	}
	checkWritesWhereReads(t, dir, link)
}

func TestAWrittenPathClimbingOutOfALinkedFolderIsWrittenWhereItReads(t *testing.T) {
	dir, nested := linkedFolder(t)
	// Windows reads root scene.json (drops "alias\.." first); POSIX reads real/scene.json.
	for _, link := range []string{filepath.Join(dir, "scene.json"), filepath.Join(nested, "..", "scene.json")} {
		if err := os.Symlink("shared.json", link); err != nil {
			t.Fatal(err)
		}
	}
	checkWritesWhereReads(t, dir, filepath.Join(dir, "alias")+string(filepath.Separator)+".."+string(filepath.Separator)+"scene.json")
}

func TestASymlinkLoopIsAnErrorAndKeepsTheLinks(t *testing.T) {
	dir := t.TempDir()
	a, b := filepath.Join(dir, "a"), filepath.Join(dir, "b")
	if err := os.Symlink("b", a); err != nil {
		t.Skip("no symlinks here:", err)
	}
	if err := os.Symlink("a", b); err != nil {
		t.Fatal(err)
	}
	if err := WriteFile(a, []byte("new"), 0o644); err == nil {
		t.Fatal("wrote through a symlink loop")
	}
	for _, f := range []string{a, b} {
		if info, err := os.Lstat(f); err != nil || info.Mode()&os.ModeSymlink == 0 {
			t.Fatalf("%s is no longer a symlink", f)
		}
	}
}

func TestAChainOfFortySymlinksIsWrittenThrough(t *testing.T) {
	dir := t.TempDir()
	for i := 1; i <= 40; i++ {
		if err := os.Symlink(fmt.Sprintf("l%d", i), filepath.Join(dir, fmt.Sprintf("l%d", i-1))); err != nil {
			t.Skip("no symlinks here:", err)
		}
	}
	if err := WriteFile(filepath.Join(dir, "l0"), []byte("new"), 0o644); err != nil {
		t.Fatal(err)
	}
	if data, _ := os.ReadFile(filepath.Join(dir, "l40")); string(data) != "new" {
		t.Fatalf("chain end holds %q, want new", data)
	}
}
