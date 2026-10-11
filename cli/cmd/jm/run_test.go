package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

func TestRunGameArchiveExtractsManifestAndResolvesEngine(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("relies on /usr/bin/true; not present on Windows")
	}
	if _, err := os.Stat("/usr/bin/true"); err != nil {
		t.Skip("/usr/bin/true not present on this host")
	}

	tmp := t.TempDir()
	t.Setenv("JM_ENGINE", "/usr/bin/true")
	man := manifest.GameManifest{
		Name:       "RunArchiveTest",
		Version:    "0",
		EntryScene: "scenes/dummy.scene.json",
		Scenes:     []string{"scenes/dummy.scene.json"},
		Assets:     []string{},
	}
	manData, _ := json.Marshal(man)

	// An archive is any file, whatever it's called; a build is a folder.
	for _, name := range []string{"test.jm", "renamed.pak"} {
		archivePath := filepath.Join(tmp, name)
		f, err := os.Create(archivePath)
		if err != nil {
			t.Fatalf("create: %v", err)
		}
		entries := []archive.AssetEntry{
			{SourcePath: ".jm.json", Type: "manifest", Payload: manData},
		}
		if err := archive.WriteArchive(f, entries); err != nil {
			t.Fatalf("WriteArchive: %v", err)
		}
		f.Close()

		if err := runGame(archivePath); err != nil {
			t.Fatalf("runGame(%s): %v", name, err)
		}
	}
}

// fakeBuild is a build folder with a manifest.
func fakeBuild(t *testing.T) string {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, archive.ManifestEntryKey), []byte(`{"name":"x","entryScene":"s","scenes":["s"]}`), 0o644)
	return dir
}

// A run that should end itself but hangs (no window server) is stopped and says why.
func TestASelfEndingRunThatHangsIsStopped(t *testing.T) {
	skipOnWindows(t)
	t.Setenv("JM_ENGINE", fakeProgram(t, t.TempDir(), "program", "sleep 5"))
	t.Setenv("JM_EXIT_AFTER_FRAMES", "1")
	t.Setenv("CODEX_SANDBOX", "")
	t.Setenv("JM_RENDERER", "")
	t.Setenv("JM_DRIVE", "0") // off, as the engine reads it
	saved := selfEndingGrace
	selfEndingGrace = 200 * time.Millisecond
	defer func() { selfEndingGrace = saved }()
	start := time.Now()
	err := runWith(fakeBuild(t), runOptions{noRecord: true})
	if err == nil || !strings.Contains(err.Error(), "hadn't run its 1 frames") || time.Since(start) > 3*time.Second {
		t.Errorf("after %v: %v", time.Since(start), err)
	}
}

func TestCodexsMacSandboxIsNamedBeforeAWindowHangs(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("macOS only")
	}
	t.Setenv("JM_ENGINE", fakeProgram(t, t.TempDir(), "program", "exit 0"))
	t.Setenv("CODEX_SANDBOX", "seatbelt")
	t.Setenv("JM_RENDERER", "")
	if err := runWith(fakeBuild(t), runOptions{noRecord: true}); err == nil || !strings.Contains(err.Error(), "escalated permissions") {
		t.Errorf("windowed: %v", err)
	}
	t.Setenv("JM_RENDERER", "none")
	if err := runWith(fakeBuild(t), runOptions{noRecord: true}); err != nil {
		t.Errorf("JM_RENDERER=none runs in the sandbox: %v", err)
	}
}
