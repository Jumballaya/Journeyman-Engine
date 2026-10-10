package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestFindEditorPrefersJMEditorThenBesideJM(t *testing.T) {
	dir := t.TempDir()
	beside := filepath.Join(dir, exeName("journeyman_editor"))
	os.WriteFile(beside, []byte("x"), 0o755)
	saved := executablePath
	executablePath = func() (string, error) { return filepath.Join(dir, "jm"), nil }
	defer func() { executablePath = saved }()
	t.Setenv("HOME", t.TempDir())

	t.Setenv("JM_EDITOR", "")
	if got, err := findEditor(); err != nil || got != beside {
		t.Errorf("beside jm: %q, %v", got, err)
	}
	pinned := filepath.Join(t.TempDir(), "my_editor")
	os.WriteFile(pinned, []byte("x"), 0o755)
	t.Setenv("JM_EDITOR", pinned)
	if got, _ := findEditor(); got != pinned {
		t.Errorf("JM_EDITOR: %q", got)
	}
	os.Remove(beside)
	t.Setenv("JM_EDITOR", "")
	if exists("/Applications/Journeyman Editor.app") {
		t.Skip("this machine has the editor installed for everyone")
	}
	if _, err := findEditor(); err == nil {
		t.Error("no editor anywhere must say so")
	}
}

func TestEditorRefusesAFolderThatIsntAGame(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "journeyman_editor"), []byte("x"), 0o755)
	t.Setenv("JM_EDITOR", filepath.Join(dir, "journeyman_editor"))
	editorCmd.SetArgs(nil)
	if err := editorCmd.RunE(editorCmd, []string{t.TempDir()}); err == nil {
		t.Error("an empty folder isn't a game")
	}
}

func TestFindEditorFindsInstallShsEditorBesideBin(t *testing.T) {
	root := t.TempDir() // install.sh: <dir>/bin/jm and <dir>/editor/
	os.MkdirAll(filepath.Join(root, "bin"), 0o755)
	os.MkdirAll(filepath.Join(root, "editor"), 0o755)
	want := filepath.Join(root, "editor", exeName("journeyman_editor"))
	os.WriteFile(want, []byte("x"), 0o755)
	saved := executablePath
	executablePath = func() (string, error) { return filepath.Join(root, "bin", "jm"), nil }
	defer func() { executablePath = saved }()
	t.Setenv("JM_EDITOR", "")
	if got, err := findEditor(); err != nil || filepath.Clean(got) != want {
		t.Errorf("got %q, %v; want %q", got, err, want)
	}

	wd, _ := os.Getwd()
	defer os.Chdir(wd)
	os.Chdir(filepath.Join(root, "editor"))
	t.Setenv("JM_EDITOR", exeName("journeyman_editor")) // relative: must come back absolute
	if got, _ := findEditor(); !filepath.IsAbs(got) {
		t.Errorf("relative JM_EDITOR stayed relative: %q", got)
	}
}
