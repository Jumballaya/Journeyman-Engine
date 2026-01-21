package manifest

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/testutil"
)

func TestLoadManifest_Valid(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	manifest := GameManifest{
		Name:       "Test Game",
		Version:    "1.0.0",
		EnginePath: "engine",
		EntryScene: "scenes/start.scene.json",
		Scenes:     []string{"scenes/start.scene.json"},
		Assets:     []string{"assets/script.script.json"},
	}

	path := filepath.Join(dir, ".jm.json")
	data, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		t.Fatalf("failed to marshal manifest: %v", err)
	}

	if err := os.WriteFile(path, data, 0644); err != nil {
		t.Fatalf("failed to write manifest: %v", err)
	}

	loaded, err := LoadManifest(path)
	if err != nil {
		t.Fatalf("LoadManifest failed: %v", err)
	}

	if loaded.Name != manifest.Name {
		t.Errorf("Name: got %q, want %q", loaded.Name, manifest.Name)
	}
	if loaded.Version != manifest.Version {
		t.Errorf("Version: got %q, want %q", loaded.Version, manifest.Version)
	}
	if len(loaded.Scenes) != len(manifest.Scenes) {
		t.Errorf("Scenes length: got %d, want %d", len(loaded.Scenes), len(manifest.Scenes))
	}
	if len(loaded.Assets) != len(manifest.Assets) {
		t.Errorf("Assets length: got %d, want %d", len(loaded.Assets), len(manifest.Assets))
	}
}

func TestLoadManifest_InvalidJSON(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	path := filepath.Join(dir, ".jm.json")
	invalidJSON := `{ "name": "test", invalid }`
	if err := os.WriteFile(path, []byte(invalidJSON), 0644); err != nil {
		t.Fatalf("failed to write invalid JSON: %v", err)
	}

	_, err := LoadManifest(path)
	if err == nil {
		t.Error("LoadManifest should fail with invalid JSON")
	}
}

func TestLoadManifest_MissingFile(t *testing.T) {
	_, err := LoadManifest("/nonexistent/path/.jm.json")
	if err == nil {
		t.Error("LoadManifest should fail for missing file")
	}
}

func TestLoadManifest_EmptyFile(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	path := filepath.Join(dir, ".jm.json")
	if err := os.WriteFile(path, []byte{}, 0644); err != nil {
		t.Fatalf("failed to write empty file: %v", err)
	}

	_, err := LoadManifest(path)
	if err == nil {
		t.Error("LoadManifest should fail for empty file")
	}
}

func TestLoadManifest_MissingFields(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	// Minimal valid manifest
	minimalJSON := `{}`
	path := filepath.Join(dir, ".jm.json")
	if err := os.WriteFile(path, []byte(minimalJSON), 0644); err != nil {
		t.Fatalf("failed to write minimal JSON: %v", err)
	}

	loaded, err := LoadManifest(path)
	if err != nil {
		t.Fatalf("LoadManifest should handle missing fields: %v", err)
	}

	// Should have empty/zero values
	if loaded.Name != "" {
		t.Errorf("Name should be empty, got %q", loaded.Name)
	}
	// Note: JSON unmarshaling may produce nil slices for missing fields
	// This is acceptable behavior - the code should handle both nil and empty slices
	if loaded.Scenes != nil && len(loaded.Scenes) != 0 {
		t.Errorf("Scenes should be empty or nil, got %v", loaded.Scenes)
	}
	if loaded.Assets != nil && len(loaded.Assets) != 0 {
		t.Errorf("Assets should be empty or nil, got %v", loaded.Assets)
	}
}

func TestLoadScriptAsset_Valid(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	asset := ScriptAsset{
		Name:    "test_script",
		Script:  "test.ts",
		Binary:  "test.wasm",
		Imports: []string{"env.foo", "env.bar"},
		Exposed: []string{"onUpdate", "onStart"},
	}

	path := filepath.Join(dir, "test.script.json")
	data, err := json.MarshalIndent(asset, "", "  ")
	if err != nil {
		t.Fatalf("failed to marshal script asset: %v", err)
	}

	if err := os.WriteFile(path, data, 0644); err != nil {
		t.Fatalf("failed to write script asset: %v", err)
	}

	loaded, err := LoadScriptAsset(path)
	if err != nil {
		t.Fatalf("LoadScriptAsset failed: %v", err)
	}

	if loaded.Name != asset.Name {
		t.Errorf("Name: got %q, want %q", loaded.Name, asset.Name)
	}
	if len(loaded.Imports) != len(asset.Imports) {
		t.Errorf("Imports length: got %d, want %d", len(loaded.Imports), len(asset.Imports))
	}
	if len(loaded.Exposed) != len(asset.Exposed) {
		t.Errorf("Exposed length: got %d, want %d", len(loaded.Exposed), len(asset.Exposed))
	}
}

func TestLoadScriptAsset_InvalidJSON(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	path := filepath.Join(dir, "test.script.json")
	invalidJSON := `{ "name": "test", invalid }`
	if err := os.WriteFile(path, []byte(invalidJSON), 0644); err != nil {
		t.Fatalf("failed to write invalid JSON: %v", err)
	}

	_, err := LoadScriptAsset(path)
	if err == nil {
		t.Error("LoadScriptAsset should fail with invalid JSON")
	}
}

func TestLoadScriptAsset_MissingFile(t *testing.T) {
	_, err := LoadScriptAsset("/nonexistent/path/script.script.json")
	if err == nil {
		t.Error("LoadScriptAsset should fail for missing file")
	}
}

func TestLoadScriptAsset_MissingOptionalFields(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	// Script asset without imports/exposed
	minimalJSON := `{
		"name": "test",
		"script": "test.ts",
		"binary": "test.wasm"
	}`
	path := filepath.Join(dir, "test.script.json")
	if err := os.WriteFile(path, []byte(minimalJSON), 0644); err != nil {
		t.Fatalf("failed to write minimal JSON: %v", err)
	}

	loaded, err := LoadScriptAsset(path)
	if err != nil {
		t.Fatalf("LoadScriptAsset should handle missing optional fields: %v", err)
	}

	// Note: JSON unmarshaling may produce nil slices for missing fields
	// This is acceptable behavior - the code should handle both nil and empty slices
	if loaded.Imports != nil && len(loaded.Imports) != 0 {
		t.Errorf("Imports should be empty or nil, got %v", loaded.Imports)
	}
	if loaded.Exposed != nil && len(loaded.Exposed) != 0 {
		t.Errorf("Exposed should be empty or nil, got %v", loaded.Exposed)
	}
}
