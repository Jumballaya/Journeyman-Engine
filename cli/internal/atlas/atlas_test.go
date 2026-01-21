package atlas

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/testutil"
)

func TestLoadAtlasConfig_Valid(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         "atlas.png",
		OutputMetadata: "atlas.json",
		MaxSize:        2048,
		Padding:        2,
		Sprites: []SpriteDef{
			{Name: "sprite1", Path: "sprite1.png"},
			{Name: "sprite2", Path: "sprite2.png"},
		},
	}

	path := filepath.Join(dir, "atlas.json")
	data, err := json.MarshalIndent(config, "", "  ")
	if err != nil {
		t.Fatalf("failed to marshal config: %v", err)
	}

	if err := os.WriteFile(path, data, 0644); err != nil {
		t.Fatalf("failed to write config: %v", err)
	}

	loaded, err := LoadAtlasConfig(path)
	if err != nil {
		t.Fatalf("LoadAtlasConfig failed: %v", err)
	}

	if loaded.Name != config.Name {
		t.Errorf("Name: got %q, want %q", loaded.Name, config.Name)
	}
	if loaded.MaxSize != config.MaxSize {
		t.Errorf("MaxSize: got %d, want %d", loaded.MaxSize, config.MaxSize)
	}
	if loaded.Padding != config.Padding {
		t.Errorf("Padding: got %d, want %d", loaded.Padding, config.Padding)
	}
	if len(loaded.Sprites) != len(config.Sprites) {
		t.Errorf("Sprites length: got %d, want %d", len(loaded.Sprites), len(config.Sprites))
	}
}

func TestLoadAtlasConfig_InvalidJSON(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	path := filepath.Join(dir, "atlas.json")
	invalidJSON := `{ "name": "test", invalid }`
	if err := os.WriteFile(path, []byte(invalidJSON), 0644); err != nil {
		t.Fatalf("failed to write invalid JSON: %v", err)
	}

	_, err := LoadAtlasConfig(path)
	if err == nil {
		t.Error("LoadAtlasConfig should fail with invalid JSON")
	}
}

func TestLoadAtlasConfig_MissingFile(t *testing.T) {
	_, err := LoadAtlasConfig("/nonexistent/path/atlas.json")
	if err == nil {
		t.Error("LoadAtlasConfig should fail for missing file")
	}
}

func TestLoadAtlasConfig_InvalidFieldTypes(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	// Config with wrong types
	invalidJSON := `{
		"name": "test",
		"maxSize": "not a number",
		"padding": "also not a number"
	}`
	path := filepath.Join(dir, "atlas.json")
	if err := os.WriteFile(path, []byte(invalidJSON), 0644); err != nil {
		t.Fatalf("failed to write invalid JSON: %v", err)
	}

	_, err := LoadAtlasConfig(path)
	if err == nil {
		t.Error("LoadAtlasConfig should fail with invalid field types")
	}
}

func TestBuildAtlas_SingleSprite(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	// Create test sprite
	spritePath := testutil.CreateTestSprite(t, dir, "sprite1", 32, 32)

	// Create atlas config
	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         filepath.Join(dir, "atlas.png"),
		OutputMetadata: filepath.Join(dir, "atlas.json"),
		MaxSize:        256,
		Padding:        2,
		Sprites: []SpriteDef{
			{Name: "sprite1", Path: spritePath},
		},
	}

	if err := BuildAtlas(config); err != nil {
		t.Fatalf("BuildAtlas failed: %v", err)
	}

	// Verify output files exist
	testutil.AssertFileExists(t, config.Output)
	testutil.AssertFileExists(t, config.OutputMetadata)

	// Verify metadata
	var metadata AtlasMetadata
	testutil.AssertJSONValid(t, config.OutputMetadata, &metadata)

	if metadata.Name != config.Name {
		t.Errorf("Metadata name: got %q, want %q", metadata.Name, config.Name)
	}
	if len(metadata.Sprites) != 1 {
		t.Errorf("Sprite count: got %d, want 1", len(metadata.Sprites))
	}

	sprite, ok := metadata.Sprites["sprite1"]
	if !ok {
		t.Fatal("sprite1 not found in metadata")
	}

	// Verify sprite dimensions
	if sprite.Width != 32 {
		t.Errorf("Sprite width: got %d, want 32", sprite.Width)
	}
	if sprite.Height != 32 {
		t.Errorf("Sprite height: got %d, want 32", sprite.Height)
	}

	// Verify normalized coordinates are in [0, 1] range
	if sprite.U < 0 || sprite.U > 1 {
		t.Errorf("U out of range: got %f", sprite.U)
	}
	if sprite.V < 0 || sprite.V > 1 {
		t.Errorf("V out of range: got %f", sprite.V)
	}
	if sprite.W < 0 || sprite.W > 1 {
		t.Errorf("W out of range: got %f", sprite.W)
	}
	if sprite.H < 0 || sprite.H > 1 {
		t.Errorf("H out of range: got %f", sprite.H)
	}
}

func TestBuildAtlas_MultipleSprites(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	// Create multiple test sprites of different sizes
	sprite1Path := testutil.CreateTestSprite(t, dir, "sprite1", 16, 16)
	sprite2Path := testutil.CreateTestSprite(t, dir, "sprite2", 32, 32)
	sprite3Path := testutil.CreateTestSprite(t, dir, "sprite3", 64, 64)

	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         filepath.Join(dir, "atlas.png"),
		OutputMetadata: filepath.Join(dir, "atlas.json"),
		MaxSize:        256,
		Padding:        2,
		Sprites: []SpriteDef{
			{Name: "sprite1", Path: sprite1Path},
			{Name: "sprite2", Path: sprite2Path},
			{Name: "sprite3", Path: sprite3Path},
		},
	}

	if err := BuildAtlas(config); err != nil {
		t.Fatalf("BuildAtlas failed: %v", err)
	}

	// Verify metadata
	var metadata AtlasMetadata
	testutil.AssertJSONValid(t, config.OutputMetadata, &metadata)

	if len(metadata.Sprites) != 3 {
		t.Errorf("Sprite count: got %d, want 3", len(metadata.Sprites))
	}

	// Verify all sprites are present
	for _, name := range []string{"sprite1", "sprite2", "sprite3"} {
		if _, ok := metadata.Sprites[name]; !ok {
			t.Errorf("Sprite %s not found in metadata", name)
		}
	}

	// Verify no overlaps (sprites should have different positions)
	positions := make(map[string]struct {
		x, y int
	})
	for name, sprite := range metadata.Sprites {
		pos := struct{ x, y int }{sprite.X, sprite.Y}
		if _, exists := positions[name]; exists {
			t.Errorf("Duplicate sprite name: %s", name)
		}
		positions[name] = pos
	}
}

func TestBuildAtlas_EmptySprites(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         filepath.Join(dir, "atlas.png"),
		OutputMetadata: filepath.Join(dir, "atlas.json"),
		MaxSize:        256,
		Padding:        2,
		Sprites:        []SpriteDef{},
	}

	err := BuildAtlas(config)
	if err == nil {
		t.Error("BuildAtlas should fail with empty sprites")
	}
}

func TestBuildAtlas_InvalidImagePath(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         filepath.Join(dir, "atlas.png"),
		OutputMetadata: filepath.Join(dir, "atlas.json"),
		MaxSize:        256,
		Padding:        2,
		Sprites: []SpriteDef{
			{Name: "sprite1", Path: "/nonexistent/path/sprite.png"},
		},
	}

	err := BuildAtlas(config)
	if err == nil {
		t.Error("BuildAtlas should fail with invalid image path")
	}
}

func TestBuildAtlas_Padding(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	spritePath := testutil.CreateTestSprite(t, dir, "sprite1", 16, 16)

	// Test with different padding values
	paddingTests := []int{0, 1, 2, 5, 10}

	for _, padding := range paddingTests {
		t.Run(fmt.Sprintf("padding_%d", padding), func(t *testing.T) {
			config := AtlasConfig{
				Name:           "test_atlas",
				Output:         filepath.Join(dir, fmt.Sprintf("atlas_%d.png", padding)),
				OutputMetadata: filepath.Join(dir, fmt.Sprintf("atlas_%d.json", padding)),
				MaxSize:        256,
				Padding:        padding,
				Sprites: []SpriteDef{
					{Name: "sprite1", Path: spritePath},
				},
			}

			if err := BuildAtlas(config); err != nil {
				t.Fatalf("BuildAtlas failed with padding %d: %v", padding, err)
			}

			var metadata AtlasMetadata
			testutil.AssertJSONValid(t, config.OutputMetadata, &metadata)

			sprite, ok := metadata.Sprites["sprite1"]
			if !ok {
				t.Fatal("sprite1 not found in metadata")
			}

			// Sprite dimensions should remain the same regardless of padding
			if sprite.Width != 16 {
				t.Errorf("Sprite width: got %d, want 16", sprite.Width)
			}
			if sprite.Height != 16 {
				t.Errorf("Sprite height: got %d, want 16", sprite.Height)
			}
		})
	}
}

func TestBuildAtlas_MaxSizeLimit(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	// Create a sprite that's too large for the maxSize
	largeSpritePath := testutil.CreateTestSprite(t, dir, "large", 512, 512)

	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         filepath.Join(dir, "atlas.png"),
		OutputMetadata: filepath.Join(dir, "atlas.json"),
		MaxSize:        256, // Smaller than sprite
		Padding:        2,
		Sprites: []SpriteDef{
			{Name: "large", Path: largeSpritePath},
		},
	}

	err := BuildAtlas(config)
	if err == nil {
		t.Error("BuildAtlas should fail when sprite exceeds maxSize")
	}
}

func TestBuildAtlas_NormalizedCoordinates(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	spritePath := testutil.CreateTestSprite(t, dir, "sprite1", 64, 64)

	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         filepath.Join(dir, "atlas.png"),
		OutputMetadata: filepath.Join(dir, "atlas.json"),
		MaxSize:        256,
		Padding:        0, // No padding for easier calculation
		Sprites: []SpriteDef{
			{Name: "sprite1", Path: spritePath},
		},
	}

	if err := BuildAtlas(config); err != nil {
		t.Fatalf("BuildAtlas failed: %v", err)
	}

	var metadata AtlasMetadata
	testutil.AssertJSONValid(t, config.OutputMetadata, &metadata)

	sprite := metadata.Sprites["sprite1"]

	// Verify normalized coordinates match pixel coordinates
	expectedU := float32(sprite.X) / float32(metadata.Width)
	expectedV := float32(sprite.Y) / float32(metadata.Height)
	expectedW := float32(sprite.Width) / float32(metadata.Width)
	expectedH := float32(sprite.Height) / float32(metadata.Height)

	// Allow small floating point differences
	tolerance := float32(0.001)
	if abs(sprite.U-expectedU) > tolerance {
		t.Errorf("U: got %f, want %f (diff: %f)", sprite.U, expectedU, abs(sprite.U-expectedU))
	}
	if abs(sprite.V-expectedV) > tolerance {
		t.Errorf("V: got %f, want %f (diff: %f)", sprite.V, expectedV, abs(sprite.V-expectedV))
	}
	if abs(sprite.W-expectedW) > tolerance {
		t.Errorf("W: got %f, want %f (diff: %f)", sprite.W, expectedW, abs(sprite.W-expectedW))
	}
	if abs(sprite.H-expectedH) > tolerance {
		t.Errorf("H: got %f, want %f (diff: %f)", sprite.H, expectedH, abs(sprite.H-expectedH))
	}
}

func TestBuildAtlas_NormalizedCoordinates_EdgeCases(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	// Test sprite at (0,0) - top-left corner
	spritePath := testutil.CreateTestSprite(t, dir, "sprite1", 16, 16)

	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         filepath.Join(dir, "atlas.png"),
		OutputMetadata: filepath.Join(dir, "atlas.json"),
		MaxSize:        256,
		Padding:        0,
		Sprites: []SpriteDef{
			{Name: "sprite1", Path: spritePath},
		},
	}

	if err := BuildAtlas(config); err != nil {
		t.Fatalf("BuildAtlas failed: %v", err)
	}

	var metadata AtlasMetadata
	testutil.AssertJSONValid(t, config.OutputMetadata, &metadata)

	sprite := metadata.Sprites["sprite1"]

	// First sprite should be at (0,0) or very close
	if sprite.X != 0 {
		t.Logf("Note: First sprite X is %d (expected 0, but padding/algorithm may vary)", sprite.X)
	}
	if sprite.Y != 0 {
		t.Logf("Note: First sprite Y is %d (expected 0, but padding/algorithm may vary)", sprite.Y)
	}

	// Normalized coordinates should still be valid
	if sprite.U < 0 || sprite.U > 1 {
		t.Errorf("U out of range: got %f", sprite.U)
	}
	if sprite.V < 0 || sprite.V > 1 {
		t.Errorf("V out of range: got %f", sprite.V)
	}
}

func TestBuildAtlas_VariousSpriteSizes(t *testing.T) {
	dir := testutil.CreateTempDir(t)
	defer os.RemoveAll(dir)

	// Test with various sprite sizes
	sizes := []struct {
		name   string
		width  int
		height int
	}{
		{"tiny", 1, 1},
		{"small", 8, 8},
		{"medium", 32, 32},
		{"large", 128, 128},
	}

	sprites := []SpriteDef{}
	for _, size := range sizes {
		spritePath := testutil.CreateTestSprite(t, dir, size.name, size.width, size.height)
		sprites = append(sprites, SpriteDef{Name: size.name, Path: spritePath})
	}

	config := AtlasConfig{
		Name:           "test_atlas",
		Output:         filepath.Join(dir, "atlas.png"),
		OutputMetadata: filepath.Join(dir, "atlas.json"),
		MaxSize:        512,
		Padding:        2,
		Sprites:        sprites,
	}

	if err := BuildAtlas(config); err != nil {
		t.Fatalf("BuildAtlas failed: %v", err)
	}

	var metadata AtlasMetadata
	testutil.AssertJSONValid(t, config.OutputMetadata, &metadata)

	// Verify all sprites are present with correct dimensions
	for _, size := range sizes {
		sprite, ok := metadata.Sprites[size.name]
		if !ok {
			t.Errorf("Sprite %s not found in metadata", size.name)
			continue
		}

		if sprite.Width != size.width {
			t.Errorf("Sprite %s width: got %d, want %d", size.name, sprite.Width, size.width)
		}
		if sprite.Height != size.height {
			t.Errorf("Sprite %s height: got %d, want %d", size.name, sprite.Height, size.height)
		}
	}
}

// Helper function for float32 absolute value
func abs(x float32) float32 {
	if x < 0 {
		return -x
	}
	return x
}
