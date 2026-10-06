// Package atlas bakes sprite atlases: it parses the user's .atlas.json configs
// and shelf-packs their source PNGs into one image, described by the
// build-output .atlas.json that engine/renderer2d/AtlasManager reads.
package atlas

import (
	"encoding/json"
	"fmt"
)

// AtlasConfig is the user-input form of .atlas.json (committed in the source
// tree). MaxSize defaults to 4096, the GL_MAX_TEXTURE_SIZE floor of the
// engine's supported GPUs; set it lower for GPUs that advertise 2048.
type AtlasConfig struct {
	Sources []string `json:"sources"`           // build-root-relative source PNGs
	Filter  string   `json:"filter,omitempty"`  // "nearest" (default) or "linear"
	Padding int      `json:"padding,omitempty"` // pixel padding between regions; default 0
	MaxSize int      `json:"maxSize,omitempty"` // atlas dimension cap; default 4096
}

// AtlasOutput is the build-output form of .atlas.json, the exact shape
// engine/renderer2d/Renderer2DModule.cpp's atlas converter parses.
type AtlasOutput struct {
	Image   string            `json:"image"`
	Width   int               `json:"width"`
	Height  int               `json:"height"`
	Filter  string            `json:"filter"`
	Regions map[string][4]int `json:"regions"` // name → [x, y, w, h] pixel rect
}

// LoadConfig parses an .atlas.json config's bytes.
func LoadConfig(data []byte) (AtlasConfig, error) {
	var c AtlasConfig
	if err := json.Unmarshal(data, &c); err != nil {
		return c, fmt.Errorf("atlas: parse config: %w", err)
	}
	return c, nil
}
