package main

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// fakeProgram writes an executable shell script at dir/name.
func fakeProgram(t *testing.T, dir, name, body string) string {
	t.Helper()
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte("#!/bin/sh\n"+body+"\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	return path
}

func skipOnWindows(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("shell-script fakes")
	}
}

func TestEngineVersionTellsANonStarterFromAnOldEngine(t *testing.T) {
	skipOnWindows(t)
	dir := t.TempDir()
	if v, err := engineVersion(fakeProgram(t, dir, "new", "echo journeyman_engine v0.0.4")); err != nil || v != "v0.0.4" {
		t.Errorf("a current engine: %q, %v", v, err)
	}
	// Before --version, the engine took it for a game it couldn't find, and exited 1.
	if v, err := engineVersion(fakeProgram(t, dir, "old", "echo no game called --version; exit 1")); err != nil || v != "" {
		t.Errorf("an engine without --version: %q, %v (want no version, no error)", v, err)
	}
	if _, err := engineVersion(fakeProgram(t, dir, "killed", "kill -9 $$")); err == nil {
		t.Error("an engine the system kills at launch must be an error, not an old engine")
	}
}

func TestDoctorSaysWhenPATHRunsAnotherJM(t *testing.T) {
	skipOnWindows(t)
	mine, other := t.TempDir(), t.TempDir()
	self := fakeProgram(t, mine, "jm", "")
	fakeProgram(t, other, "jm", "")
	saved := executablePath
	executablePath = func() (string, error) { return self, nil }
	defer func() { executablePath = saved }()

	t.Setenv("PATH", other+string(os.PathListSeparator)+mine)
	r := doctorReport{OK: true}
	r.checkInstall(self)
	if !hasProblem(r, "PATH runs another jm first") {
		t.Errorf("problems: %+v", r.Problems)
	}
	t.Setenv("PATH", t.TempDir())
	r = doctorReport{OK: true}
	r.checkInstall(self)
	if !hasProblem(r, "isn't on PATH") {
		t.Errorf("problems: %+v", r.Problems)
	}
}

func hasProblem(r doctorReport, text string) bool {
	for _, p := range r.Problems {
		if strings.Contains(p.Message, text) {
			return true
		}
	}
	return false
}

func TestDoctorFixesQuotePathsWithSpaces(t *testing.T) {
	dir := "/Applications/Journeyman Editor.app/Contents/MacOS"
	t.Setenv("PATH", "")
	r := doctorReport{OK: true}
	r.checkInstall(filepath.Join(dir, "jm"))
	if len(r.Problems) == 0 || !strings.Contains(r.Problems[0].Fix, `'`+dir+`'`) {
		t.Errorf("the PATH fix must quote the folder: %+v", r.Problems)
	}
	if runtime.GOOS == "darwin" && !strings.Contains(unblockFix(dir), `'`+dir+`'`) {
		t.Errorf("unquoted: %s", unblockFix(dir))
	}
	if got := shellQuote("it's"); got != `'it'\''s'` {
		t.Errorf("shellQuote: %s", got)
	}
}
