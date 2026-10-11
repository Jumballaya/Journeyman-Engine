package main

import (
	"encoding/binary"
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
	if got := shellQuote("it's"); got != `'it'\''s'` {
		t.Errorf("shellQuote: %s", got)
	}
}

func TestOlderVersion(t *testing.T) {
	for _, c := range []struct {
		a, b  string
		older bool
	}{{"15.5", "26.0", true}, {"26.0", "11.0", false}, {"11.0", "11", false}, {"13.2.1", "13.3", true}, {"26.6.2", "26.6", false}, {"26.0", "26.0.1", true}} {
		if got := olderVersion(c.a, c.b); got != c.older {
			t.Errorf("%s before %s: %v", c.a, c.b, got)
		}
	}
}

// jm's own binary says which macOS it needs, as the engine's would.
func TestMachoMinOSReadsABinarysMinimum(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("Mach-O only")
	}
	self, _ := os.Executable()
	if v := machoMinOS(self); v == "" || olderVersion(v, "10.0") {
		t.Errorf("minimum macOS of %s: %q", self, v)
	}
	if v := machoMinOS("doctor.go"); v != "" {
		t.Errorf("a text file: %q", v)
	}
}

// Both load commands that carry the minimum keep a nonzero patch ("26.0.1").
func TestMachoMinOSKeepsThePatch(t *testing.T) {
	const v2601, v11 = 26<<16 | 1, 11 << 16
	for _, c := range []struct {
		cmd  []uint32
		want string
	}{
		{[]uint32{0x32, 24, 1, v2601, 0, 0}, "26.0.1"}, // LC_BUILD_VERSION
		{[]uint32{0x24, 16, v2601, 0}, "26.0.1"},       // LC_VERSION_MIN_MACOSX
		{[]uint32{0x32, 24, 1, v11, 0, 0}, "11.0"},
		{[]uint32{0x24, 16, v11, 0}, "11.0"},
	} {
		// A 64-bit Mach-O header (magic, arm64, executable, one command) then the command.
		words := append([]uint32{0xfeedfacf, 0x0100000c, 0, 2, 1, uint32(4 * len(c.cmd)), 0, 0}, c.cmd...)
		data := make([]byte, 4*len(words))
		for i, w := range words {
			binary.LittleEndian.PutUint32(data[4*i:], w)
		}
		path := filepath.Join(t.TempDir(), "program")
		os.WriteFile(path, data, 0o755)
		if got := machoMinOS(path); got != c.want {
			t.Errorf("command %#x: %q, want %q", c.cmd[0], got, c.want)
		}
	}
}

// A build for a newer macOS is named as such, not taken for a quarantine.
func TestStartFixNamesTheMacOSABuildNeeds(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("macOS only")
	}
	self, _ := os.Executable()
	data, err := os.ReadFile(self)
	if err != nil {
		t.Fatal(err)
	}
	// Raise LC_BUILD_VERSION's minos to 99.0 (a 64-bit header is 32 bytes).
	le := binary.LittleEndian
	for off, n := uint32(32), le.Uint32(data[16:]); n > 0; n-- {
		if le.Uint32(data[off:]) == 0x32 {
			le.PutUint32(data[off+12:], 99<<16)
			break
		}
		off += le.Uint32(data[off+4:])
	}
	engine := filepath.Join(t.TempDir(), "journeyman_engine")
	os.WriteFile(engine, data, 0o755)
	why, fix := startFix(engine)
	if !strings.Contains(why, "needs macOS 99.0") || !strings.Contains(fix, "update macOS") {
		t.Errorf("why %q, fix %q", why, fix)
	}
}
