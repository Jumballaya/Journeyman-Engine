package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"testing"

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
