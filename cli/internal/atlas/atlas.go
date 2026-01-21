// Package atlas provides functionality for building sprite atlases from individual sprite images.
// An atlas is a single texture image containing multiple sprites, along with metadata describing
// where each sprite is located within the atlas. This reduces draw calls and improves rendering performance.
//
// The atlas building process:
//  1. Load individual sprite images from disk
//  2. Pack sprites into a single texture using a bin-packing algorithm
//  3. Generate normalized texture coordinates for the engine
//  4. Save the atlas image and metadata JSON file
//
// The metadata format matches the engine's texture coordinate system, which uses normalized
// coordinates [u, v, w, h] in the range [0, 1] for the texRect parameter.
package atlas

import (
	"encoding/json"
	"fmt"
	"image"
	"image/draw"
	"image/png"
	"os"
	"path/filepath"
	"sort"

	_ "image/jpeg"
	_ "image/png"
)

// AtlasConfig represents the input configuration for building an atlas.
// This is typically loaded from a .atlas.json file.
//
// Example JSON structure:
//   {
//     "name": "main_sprites",
//     "output": "assets/atlases/main_sprites.png",
//     "outputMetadata": "assets/atlases/main_sprites.atlas.json",
//     "maxSize": 2048,
//     "padding": 2,
//     "sprites": [
//       { "name": "sprite1", "path": "assets/sprites/sprite1.png" },
//       { "name": "sprite2", "path": "assets/sprites/sprite2.png" }
//     ]
//   }
type AtlasConfig struct {
	// Name is the identifier for this atlas (used in metadata)
	Name string `json:"name"`

	// Output is the file path where the atlas image will be saved (PNG format)
	Output string `json:"output"`

	// OutputMetadata is the file path where the atlas metadata JSON will be saved
	OutputMetadata string `json:"outputMetadata"`

	// MaxSize is the maximum width/height of the atlas in pixels.
	// The atlas will expand up to this size, but will fail if sprites cannot fit.
	// Common values: 512, 1024, 2048, 4096
	MaxSize int `json:"maxSize"`

	// Padding is the number of pixels to add around each sprite in the atlas.
	// This prevents texture bleeding when filtering. Recommended: 1-2 pixels.
	Padding int `json:"padding"`

	// Sprites is the list of sprite images to pack into the atlas.
	// Each sprite must exist as a file on disk.
	Sprites []SpriteDef `json:"sprites"`
}

// SpriteDef defines a single sprite to be included in the atlas.
type SpriteDef struct {
	// Name is the unique identifier for this sprite (used as key in metadata)
	Name string `json:"name"`

	// Path is the file system path to the sprite image file.
	// Supported formats: PNG, JPEG
	Path string `json:"path"`
}

// SpriteRect describes the position and size of a sprite within the atlas.
// It contains both pixel coordinates (for reference/debugging) and normalized
// coordinates (for the engine's texRect parameter).
//
// The normalized coordinates [u, v, w, h] match the engine's SpriteInstance.texRect format:
//   - u, v: Top-left corner position in normalized coordinates [0, 1]
//   - w, h: Width and height in normalized coordinates [0, 1]
//
// Example: A sprite at pixel position (64, 32) with size (128, 128) in a 512x512 atlas:
//   - Pixel: x=64, y=32, width=128, height=128
//   - Normalized: u=0.125, v=0.0625, w=0.25, h=0.25
type SpriteRect struct {
	// Pixel coordinates (for reference/debugging)
	X      int `json:"x"`      // X position in pixels from left edge
	Y      int `json:"y"`      // Y position in pixels from top edge
	Width  int `json:"width"`  // Width in pixels
	Height int `json:"height"` // Height in pixels

	// Normalized coordinates [u, v, w, h] for engine (0-1 range)
	// These match the engine's texRect format in SpriteInstance
	U float32 `json:"u"` // Normalized X position (0.0 = left, 1.0 = right)
	V float32 `json:"v"` // Normalized Y position (0.0 = top, 1.0 = bottom)
	W float32 `json:"w"` // Normalized width (0.0 = none, 1.0 = full atlas width)
	H float32 `json:"h"` // Normalized height (0.0 = none, 1.0 = full atlas height)
}

// AtlasMetadata is the output structure written to the metadata JSON file.
// This file is consumed by the engine to look up sprite positions in the atlas.
//
// Example JSON structure:
//   {
//     "name": "main_sprites",
//     "width": 512,
//     "height": 512,
//     "sprites": {
//       "sprite1": {
//         "x": 0, "y": 0, "width": 64, "height": 64,
//         "u": 0.0, "v": 0.0, "w": 0.125, "h": 0.125
//       },
//       "sprite2": {
//         "x": 66, "y": 0, "width": 32, "height": 32,
//         "u": 0.1289, "v": 0.0, "w": 0.0625, "h": 0.0625
//       }
//     }
//   }
//
// The engine uses the normalized coordinates (u, v, w, h) to construct the texRect
// parameter when drawing sprites. The pixel coordinates (x, y, width, height) are
// included for debugging and tooling purposes.
type AtlasMetadata struct {
	// Name matches the AtlasConfig.Name
	Name string `json:"name"`

	// Width is the final width of the atlas image in pixels
	Width int `json:"width"`

	// Height is the final height of the atlas image in pixels
	Height int `json:"height"`

	// Sprites is a map of sprite names to their positions and dimensions.
	// The key is the sprite name from SpriteDef.Name.
	// The value contains both pixel and normalized coordinates.
	Sprites map[string]SpriteRect `json:"sprites"`
}

// spriteImage is an internal type representing a loaded sprite image with its bounds.
type spriteImage struct {
	name   string
	img    image.Image
	bounds image.Rectangle
}

// packedSprite is an internal type representing a sprite that has been positioned in the atlas.
type packedSprite struct {
	sprite spriteImage
	x      int // Final X position in the atlas (after padding)
	y      int // Final Y position in the atlas (after padding)
}

// LoadAtlasConfig loads and parses an atlas configuration from a JSON file.
//
// The JSON file must contain a valid AtlasConfig structure. Returns an error if:
//   - The file cannot be read
//   - The JSON is invalid or malformed
//   - Required fields are missing or have wrong types
//
// Example usage:
//   config, err := LoadAtlasConfig("assets/atlases/main.atlas.json")
//   if err != nil {
//       return err
//   }
func LoadAtlasConfig(path string) (AtlasConfig, error) {
	var config AtlasConfig
	data, err := os.ReadFile(path)
	if err != nil {
		return config, fmt.Errorf("failed to read atlas config: %w", err)
	}

	if err := json.Unmarshal(data, &config); err != nil {
		return config, fmt.Errorf("failed to parse atlas config: %w", err)
	}

	return config, nil
}

// BuildAtlas creates a sprite atlas from the provided configuration.
//
// The build process:
//  1. Loads all sprite images from disk
//  2. Sorts sprites by area (largest first) for optimal packing
//  3. Packs sprites into a single texture using bin-packing algorithm
//  4. Calculates normalized texture coordinates for each sprite
//  5. Renders all sprites onto the atlas image
//  6. Saves the atlas PNG image and metadata JSON file
//
// Returns an error if:
//   - Any sprite image cannot be loaded
//   - Sprites cannot fit within maxSize
//   - Output files cannot be written
//
// The output files:
//   - Atlas image: A single PNG containing all sprites
//   - Metadata JSON: Contains sprite positions and normalized coordinates
//
// Example usage:
//   config := AtlasConfig{...}
//   if err := BuildAtlas(config); err != nil {
//       return fmt.Errorf("failed to build atlas: %w", err)
//   }
func BuildAtlas(config AtlasConfig) error {
	// Step 1: Load all sprite images from disk
	// Each sprite is loaded and its bounds are stored for packing calculations
	sprites := make([]spriteImage, 0, len(config.Sprites))
	for _, spriteDef := range config.Sprites {
		img, err := loadImage(spriteDef.Path)
		if err != nil {
			return fmt.Errorf("failed to load sprite %s (%s): %w", spriteDef.Name, spriteDef.Path, err)
		}
		sprites = append(sprites, spriteImage{
			name:   spriteDef.Name,
			img:    img,
			bounds: img.Bounds(),
		})
	}

	// Step 2: Sort sprites by area (largest first) for better packing efficiency
	// Larger sprites are placed first, leaving smaller sprites to fill gaps
	sort.Slice(sprites, func(i, j int) bool {
		areaI := sprites[i].bounds.Dx() * sprites[i].bounds.Dy()
		areaJ := sprites[j].bounds.Dx() * sprites[j].bounds.Dy()
		return areaI > areaJ
	})

	// Step 3: Pack sprites using a simple bottom-left bin-packing algorithm
	// The algorithm tries to place each sprite, expanding the atlas if needed
	// up to maxSize. Returns the final atlas dimensions and sprite positions.
	packed, width, height, err := packSprites(sprites, config.MaxSize, config.Padding)
	if err != nil {
		return fmt.Errorf("failed to pack sprites: %w", err)
	}

	// Step 4: Create the output atlas image
	atlasImg := image.NewRGBA(image.Rect(0, 0, width, height))

	// Step 5: Initialize metadata structure
	// This will be populated with sprite positions and saved as JSON
	metadata := AtlasMetadata{
		Name:    config.Name,
		Width:   width,
		Height:  height,
		Sprites: make(map[string]SpriteRect),
	}

	// Step 6: Draw all sprites onto the atlas and calculate coordinates
	for _, packed := range packed {
		spriteWidth := packed.sprite.bounds.Dx()
		spriteHeight := packed.sprite.bounds.Dy()

		// Draw the sprite image onto the atlas at its calculated position
		draw.Draw(atlasImg, image.Rect(packed.x, packed.y, packed.x+spriteWidth, packed.y+spriteHeight),
			packed.sprite.img, packed.sprite.bounds.Min, draw.Src)

		// Calculate normalized coordinates [u, v, w, h] for the engine
		// These coordinates are in the range [0, 1] and match the engine's texRect format
		// u, v: Top-left corner position (normalized)
		// w, h: Width and height (normalized)
		u := float32(packed.x) / float32(width)
		v := float32(packed.y) / float32(height)
		w := float32(spriteWidth) / float32(width)
		h := float32(spriteHeight) / float32(height)

		// Store both pixel and normalized coordinates in metadata
		// Pixel coordinates: For reference, debugging, and tooling
		// Normalized coordinates: For the engine's texRect parameter (matches SpriteInstance format)
		metadata.Sprites[packed.sprite.name] = SpriteRect{
			X:      packed.x,
			Y:      packed.y,
			Width:  spriteWidth,
			Height: spriteHeight,
			U:      u, // Normalized u (0-1): X position / atlas width
			V:      v, // Normalized v (0-1): Y position / atlas height
			W:      w, // Normalized width (0-1): sprite width / atlas width
			H:      h, // Normalized height (0-1): sprite height / atlas height
		}
	}

	// Step 7: Save the atlas image as a PNG file
	if err := saveImage(config.Output, atlasImg); err != nil {
		return fmt.Errorf("failed to save atlas image: %w", err)
	}

	// Step 8: Save the metadata as a JSON file
	// The metadata contains sprite positions and is used by the engine to look up sprites
	metadataJSON, err := json.MarshalIndent(metadata, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshal metadata: %w", err)
	}

	// Ensure the output directory exists
	if err := os.MkdirAll(filepath.Dir(config.OutputMetadata), os.ModePerm); err != nil {
		return fmt.Errorf("failed to create metadata directory: %w", err)
	}

	// Write the metadata JSON file
	if err := os.WriteFile(config.OutputMetadata, metadataJSON, 0644); err != nil {
		return fmt.Errorf("failed to write metadata: %w", err)
	}

	return nil
}

// loadImage loads an image file from disk.
// Supports PNG and JPEG formats (via image package decoders).
//
// Returns the decoded image and any error encountered during loading.
func loadImage(path string) (image.Image, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	img, _, err := image.Decode(file)
	if err != nil {
		return nil, err
	}

	return img, nil
}

// saveImage saves an image to disk as a PNG file.
// Creates the output directory if it doesn't exist.
//
// Returns an error if the directory cannot be created or the file cannot be written.
func saveImage(path string, img image.Image) error {
	if err := os.MkdirAll(filepath.Dir(path), os.ModePerm); err != nil {
		return err
	}

	file, err := os.Create(path)
	if err != nil {
		return err
	}
	defer file.Close()

	return png.Encode(file, img)
}

// packSprites uses a simple bottom-left bin-packing algorithm to arrange sprites in the atlas.
//
// Algorithm:
//  1. Start with a small initial atlas size (64x64)
//  2. For each sprite (sorted largest to smallest):
//     a. Try to find a position where the sprite fits without overlapping existing sprites
//     b. If no position found, expand the atlas (double width, then double height)
//     c. Retry placement after expansion
//     d. If still can't place after reaching maxSize, return error
//  3. Return the final atlas dimensions and all sprite positions
//
// The padding is added around each sprite to prevent texture bleeding when filtering.
// The actual sprite is drawn at (x+padding, y+padding) in the final atlas.
//
// Parameters:
//   - sprites: List of sprite images to pack (should be sorted by area, largest first)
//   - maxSize: Maximum atlas width/height in pixels
//   - padding: Pixels of padding around each sprite
//
// Returns:
//   - []packedSprite: List of positioned sprites with their final (x, y) coordinates
//   - int: Final atlas width in pixels
//   - int: Final atlas height in pixels
//   - error: Error if sprites cannot fit within maxSize
func packSprites(sprites []spriteImage, maxSize, padding int) ([]packedSprite, int, int, error) {
	if len(sprites) == 0 {
		return nil, 0, 0, fmt.Errorf("no sprites to pack")
	}

	packed := make([]packedSprite, 0, len(sprites))
	usedRects := []image.Rectangle{}

	// Start with a small initial size
	atlasWidth := 64
	atlasHeight := 64

	for _, sprite := range sprites {
		spriteWidth := sprite.bounds.Dx() + padding*2
		spriteHeight := sprite.bounds.Dy() + padding*2

		// Keep trying to place the sprite, expanding the atlas as needed
		placed := false
		for !placed {
			// Try to find a position for this sprite
			for y := 0; y <= atlasHeight-spriteHeight && !placed; y++ {
				for x := 0; x <= atlasWidth-spriteWidth && !placed; x++ {
					rect := image.Rect(x, y, x+spriteWidth, y+spriteHeight)

					// Check if this position overlaps with any placed sprite
					overlaps := false
					for _, used := range usedRects {
						if rectsOverlap(rect, used) {
							overlaps = true
							break
						}
					}

					if !overlaps {
						packed = append(packed, packedSprite{
							sprite: sprite,
							x:      x + padding,
							y:      y + padding,
						})
						usedRects = append(usedRects, rect)
						placed = true
					}
				}
			}

			// If we couldn't place it, expand the atlas and retry
			if !placed {
				expanded := false
				// Try expanding width first
				if atlasWidth < maxSize {
					atlasWidth = min(atlasWidth*2, maxSize)
					expanded = true
				} else if atlasHeight < maxSize {
					atlasHeight = min(atlasHeight*2, maxSize)
					expanded = true
				}

				if !expanded {
					// Can't expand further - sprite won't fit
					return nil, 0, 0, fmt.Errorf("atlas size exceeded maxSize (%d) while packing sprite %s", maxSize, sprite.name)
				}
				// Continue loop to retry placement with expanded atlas
			}
		}
	}

	return packed, atlasWidth, atlasHeight, nil
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// rectsOverlap checks if two rectangles overlap.
// Two rectangles overlap if they share any common area.
//
// This is used during packing to ensure sprites don't overlap in the atlas.
func rectsOverlap(r1, r2 image.Rectangle) bool {
	return r1.Min.X < r2.Max.X && r1.Max.X > r2.Min.X &&
		r1.Min.Y < r2.Max.Y && r1.Max.Y > r2.Min.Y
}
