package testutil

import (
	"bytes"
	"encoding/json"
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
)

// CreateTempDir creates a temporary directory for testing
func CreateTempDir(t testingT) string {
	dir, err := os.MkdirTemp("", "jm-test-*")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	return dir
}

// CreateTestManifest creates a valid test manifest file
func CreateTestManifest(t testingT, dir, name string) string {
	// Use a map to avoid importing manifest package (prevents import cycle)
	manifest := map[string]interface{}{
		"name":       name,
		"version":    "0.1.0",
		"engine":     "../build/engine/journeyman_engine",
		"entryString": "scenes/level1.scene.json",
		"scenes":     []string{"scenes/level1.scene.json"},
		"assets":     []string{},
	}

	path := filepath.Join(dir, ".jm.json")
	data, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		t.Fatalf("failed to marshal manifest: %v", err)
	}

	if err := os.WriteFile(path, data, 0644); err != nil {
		t.Fatalf("failed to write manifest: %v", err)
	}

	return path
}

// CreateTestScriptAsset creates a test script asset file
func CreateTestScriptAsset(t testingT, dir, name string) (string, string) {
	scriptPath := filepath.Join(dir, name+".ts")
	scriptContent := `export function onUpdate(dt: f32): void {}`
	if err := os.WriteFile(scriptPath, []byte(scriptContent), 0644); err != nil {
		t.Fatalf("failed to write script: %v", err)
	}

	assetPath := filepath.Join(dir, name+".script.json")
	// Use a map to avoid importing manifest package
	asset := map[string]interface{}{
		"name":    name,
		"script":  name + ".ts",
		"binary":  name + ".wasm",
		"imports": []string{},
		"exposed": []string{},
	}

	data, err := json.MarshalIndent(asset, "", "  ")
	if err != nil {
		t.Fatalf("failed to marshal script asset: %v", err)
	}

	if err := os.WriteFile(assetPath, data, 0644); err != nil {
		t.Fatalf("failed to write script asset: %v", err)
	}

	return assetPath, scriptPath
}

// CreateTestPNG creates a test PNG image in memory
func CreateTestPNG(width, height int) ([]byte, error) {
	img := image.NewRGBA(image.Rect(0, 0, width, height))
	
	// Fill with a simple pattern
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			c := color.RGBA{
				R: uint8((x + y) % 256),
				G: uint8((x * 2) % 256),
				B: uint8((y * 2) % 256),
				A: 255,
			}
			img.Set(x, y, c)
		}
	}

	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// CreateTestSprite creates a test sprite PNG file
func CreateTestSprite(t testingT, dir, name string, width, height int) string {
	img := image.NewRGBA(image.Rect(0, 0, width, height))
	
	// Fill with a simple pattern
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			c := color.RGBA{
				R: uint8((x + y) % 256),
				G: uint8((x * 2) % 256),
				B: uint8((y * 2) % 256),
				A: 255,
			}
			img.Set(x, y, c)
		}
	}

	path := filepath.Join(dir, name+".png")
	file, err := os.Create(path)
	if err != nil {
		t.Fatalf("failed to create sprite file: %v", err)
	}
	defer file.Close()

	if err := png.Encode(file, img); err != nil {
		t.Fatalf("failed to encode PNG: %v", err)
	}

	return path
}

// AssertFileExists verifies a file exists
func AssertFileExists(t testingT, path string) {
	if _, err := os.Stat(path); os.IsNotExist(err) {
		t.Fatalf("file does not exist: %s", path)
	}
}

// AssertFileNotExists verifies a file does not exist
func AssertFileNotExists(t testingT, path string) {
	if _, err := os.Stat(path); err == nil {
		t.Fatalf("file should not exist: %s", path)
	}
}

// AssertFileContent verifies file content matches expected
func AssertFileContent(t testingT, path string, expected []byte) {
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("failed to read file: %v", err)
	}

	// Simple byte comparison - could be enhanced for text files
	if len(data) != len(expected) {
		t.Fatalf("file size mismatch: got %d, want %d", len(data), len(expected))
	}

	for i := range data {
		if data[i] != expected[i] {
			t.Fatalf("file content mismatch at byte %d", i)
		}
	}
}

// AssertJSONValid verifies JSON is valid and can be unmarshaled
func AssertJSONValid(t testingT, path string, v interface{}) {
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("failed to read JSON file: %v", err)
	}

	if err := json.Unmarshal(data, v); err != nil {
		t.Fatalf("invalid JSON: %v", err)
	}
}

// testingT is a minimal interface for testing.T and testing.B
type testingT interface {
	Fatalf(format string, args ...interface{})
}
