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

func TestRunArchiveExtractsManifestAndResolvesEngine(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("relies on /usr/bin/true; not present on Windows")
	}
	if _, err := os.Stat("/usr/bin/true"); err != nil {
		t.Skip("/usr/bin/true not present on this host")
	}

	tmp := t.TempDir()
	man := manifest.GameManifest{
		Name:       "RunArchiveTest",
		Version:    "0",
		EnginePath: "/usr/bin/true",
		EntryScene: "scenes/dummy.scene.json",
		Scenes:     []string{"scenes/dummy.scene.json"},
		Assets:     []string{},
	}
	manData, _ := json.Marshal(man)

	archivePath := filepath.Join(tmp, "test.jm")
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

	if err := runArchive(archivePath); err != nil {
		t.Fatalf("runArchive: %v", err)
	}
}
