// Package atlas implements the CLI's atlas-baking pipeline: parse user-input
// .atlas.json configs, decode the listed source PNGs, shelf-pack them into a
// single atlas image, and emit the build-output .atlas.json (image path,
// dimensions, per-region rects).
//
// The runtime side (engine/renderer2d/AtlasManager) consumes the build-output
// form. This package only ever produces it.
package atlas

import (
	"encoding/json"
	"fmt"
)

// AtlasConfig is the user-input form of .atlas.json (committed in the source
// tree). The baker reads this, packs the sources, and emits AtlasOutput.
//
// MaxSize defaults to 4096 — the OpenGL 4.1 GL_MAX_TEXTURE_SIZE floor on the
// engine's supported GPU set. Lower-end GPUs (advertising 2048) require an
// explicit override. The build itself is GPU-agnostic; runtime cap mismatch
// surfaces as an engine-side load error.
type AtlasConfig struct {
	Sources []string `json:"sources"`           // build-root-relative source PNGs
	Filter  string   `json:"filter,omitempty"`  // "nearest" (default) or "linear"
	Padding int      `json:"padding,omitempty"` // pixel padding between regions; default 0
	MaxSize int      `json:"maxSize,omitempty"` // atlas dimension cap; default 4096
}

// AtlasOutput is the build-output form of .atlas.json. Engine-side schema —
// engine/renderer2d/Renderer2DModule.cpp's atlas converter parses this exact
// shape.
type AtlasOutput struct {
	Image   string            `json:"image"`
	Width   int               `json:"width"`
	Height  int               `json:"height"`
	Filter  string            `json:"filter"`
	Regions map[string][4]int `json:"regions"` // name → [x, y, w, h] pixel rect
}

// LoadConfig parses raw bytes into an AtlasConfig. Caller is responsible for
// reading the file (build.go uses os.ReadFile so cwd-relative paths work
// without this package taking an FS dependency).
func LoadConfig(data []byte) (AtlasConfig, error) {
	var c AtlasConfig
	if err := json.Unmarshal(data, &c); err != nil {
		return c, fmt.Errorf("atlas: parse config: %w", err)
	}
	return c, nil
}
