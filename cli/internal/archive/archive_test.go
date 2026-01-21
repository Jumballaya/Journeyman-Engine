package archive

import (
	"archive/zip"
	"os"
	"path/filepath"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/testutil"
)

func TestCreateArchive_Valid(t *testing.T) {
	buildDir := testutil.CreateTempDir(t)
	defer os.RemoveAll(buildDir)

	// Create a valid build directory structure
	manifestPath := filepath.Join(buildDir, ".jm.json")
	// Write manifest JSON
	manifestJSON := `{"name":"Test Game","version":"1.0.0","engine":"engine","entryString":"scenes/start.scene.json","scenes":["scenes/start.scene.json"],"assets":["assets/script.script.json"]}`
	os.WriteFile(manifestPath, []byte(manifestJSON), 0644)

	// Create scene file
	sceneDir := filepath.Join(buildDir, "scenes")
	os.MkdirAll(sceneDir, 0755)
	sceneFile := filepath.Join(sceneDir, "start.scene.json")
	os.WriteFile(sceneFile, []byte(`{"name": "Start"}`), 0644)

	// Create asset file
	assetDir := filepath.Join(buildDir, "assets")
	os.MkdirAll(assetDir, 0755)
	assetFile := filepath.Join(assetDir, "script.script.json")
	os.WriteFile(assetFile, []byte(`{"name": "script"}`), 0644)

	// Create archive
	archivePath := filepath.Join(buildDir, "test.jm")
	if err := CreateArchive(buildDir, archivePath); err != nil {
		t.Fatalf("CreateArchive failed: %v", err)
	}

	// Verify archive exists
	testutil.AssertFileExists(t, archivePath)

	// Verify archive structure
	reader, err := zip.OpenReader(archivePath)
	if err != nil {
		t.Fatalf("failed to open archive: %v", err)
	}
	defer reader.Close()

	foundManifest := false
	foundScene := false
	foundAsset := false

	for _, file := range reader.File {
		switch file.Name {
		case ".jm.json":
			foundManifest = true
		case "scenes/start.scene.json":
			foundScene = true
		case "assets/script.script.json":
			foundAsset = true
		}
	}

	if !foundManifest {
		t.Error("manifest not found in archive")
	}
	if !foundScene {
		t.Error("scene not found in archive")
	}
	if !foundAsset {
		t.Error("asset not found in archive")
	}
}

func TestCreateArchive_MissingManifest(t *testing.T) {
	buildDir := testutil.CreateTempDir(t)
	defer os.RemoveAll(buildDir)

	archivePath := filepath.Join(buildDir, "test.jm")
	err := CreateArchive(buildDir, archivePath)
	if err == nil {
		t.Error("CreateArchive should fail with missing manifest")
	}
}

func TestCreateArchive_EmptyAssets(t *testing.T) {
	buildDir := testutil.CreateTempDir(t)
	defer os.RemoveAll(buildDir)

	// Create manifest with empty assets and scenes
	manifestPath := filepath.Join(buildDir, ".jm.json")
	// Write manifest JSON with empty assets and scenes
	manifestJSON := `{"name":"Test Game","version":"1.0.0","engine":"engine","entryString":"","scenes":[],"assets":[]}`
	os.WriteFile(manifestPath, []byte(manifestJSON), 0644)

	archivePath := filepath.Join(buildDir, "test.jm")
	if err := CreateArchive(buildDir, archivePath); err != nil {
		t.Fatalf("CreateArchive should handle empty assets: %v", err)
	}

	testutil.AssertFileExists(t, archivePath)
}

func TestExtractArchive_Valid(t *testing.T) {
	// First create an archive
	buildDir := testutil.CreateTempDir(t)
	defer os.RemoveAll(buildDir)

	manifestPath := filepath.Join(buildDir, ".jm.json")
	os.WriteFile(manifestPath, []byte(`{"name": "Test"}`), 0644)

	archivePath := filepath.Join(buildDir, "test.jm")
	if err := CreateArchive(buildDir, archivePath); err != nil {
		t.Fatalf("failed to create archive: %v", err)
	}

	// Extract to new directory
	extractDir := testutil.CreateTempDir(t)
	defer os.RemoveAll(extractDir)

	if err := ExtractArchive(archivePath, extractDir); err != nil {
		t.Fatalf("ExtractArchive failed: %v", err)
	}

	// Verify extracted files
	extractedManifest := filepath.Join(extractDir, ".jm.json")
	testutil.AssertFileExists(t, extractedManifest)
}

func TestExtractArchive_Corrupted(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	corruptedPath := filepath.Join(dir, "corrupted.jm")
	os.WriteFile(corruptedPath, []byte("not a zip file"), 0644)

	extractDir := testutil.CreateTempDir(t)
	defer os.RemoveAll(extractDir)

	err := ExtractArchive(corruptedPath, extractDir)
	if err == nil {
		t.Error("ExtractArchive should fail with corrupted archive")
	}
}

func TestIsArchive_ValidExtensions(t *testing.T) {
	tests := []struct {
		path     string
		expected bool
	}{
		{"game.jm", true},
		{"game.JM", true}, // Case insensitive
		{"path/to/game.jm", true},
		{"game.json", false},
		{"game.wasm", false},
		{"game", false},
		{".jm", true}, // IsArchive checks suffix, so this matches (implementation detail)
	}

	for _, tt := range tests {
		t.Run(tt.path, func(t *testing.T) {
			result := IsArchive(tt.path)
			if result != tt.expected {
				t.Errorf("IsArchive(%q) = %v, want %v", tt.path, result, tt.expected)
			}
		})
	}
}

func TestCreateArchive_NestedDirectories(t *testing.T) {
	buildDir := testutil.CreateTempDir(t)
	defer os.RemoveAll(buildDir)

	// Create nested directory structure
	nestedPath := filepath.Join(buildDir, "assets", "scripts", "nested")
	os.MkdirAll(nestedPath, 0755)
	nestedFile := filepath.Join(nestedPath, "file.txt")
	os.WriteFile(nestedFile, []byte("content"), 0644)

	manifestPath := filepath.Join(buildDir, ".jm.json")
	// Write manifest JSON
	manifestJSON := `{"name":"Test","version":"1.0.0","engine":"engine","entryString":"scenes/start.scene.json","scenes":[],"assets":["assets/scripts/nested/file.txt"]}`
	os.WriteFile(manifestPath, []byte(manifestJSON), 0644)

	archivePath := filepath.Join(buildDir, "test.jm")
	if err := CreateArchive(buildDir, archivePath); err != nil {
		t.Fatalf("CreateArchive failed: %v", err)
	}

	// Extract and verify
	extractDir := testutil.CreateTempDir(t)
	defer os.RemoveAll(extractDir)

	if err := ExtractArchive(archivePath, extractDir); err != nil {
		t.Fatalf("ExtractArchive failed: %v", err)
	}

	extractedFile := filepath.Join(extractDir, "assets", "scripts", "nested", "file.txt")
	testutil.AssertFileExists(t, extractedFile)
}
