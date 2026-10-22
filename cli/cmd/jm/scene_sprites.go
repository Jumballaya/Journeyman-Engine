package main

import (
	"bytes"
	"encoding/base64"
	"image"
	"image/png"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
)

// pictureBudget caps the pictures one scene_view sends: past it, sprites and
// maps are drawn as their boxes.
const pictureBudget = 8 << 20

// scenePictures is how the scene's sprites and tile maps look, for the scene
// page: each sprite's look (the image the engine draws it with, and the part:
// lookImage's) as a PNG data URI; each tile map's .tmj drawn whole, with its
// size in pixels. One it can't read is left out (the page draws its box).
func scenePictures(root string, placed []placedEntity) (sprites map[string]string, maps map[string]map[string]any) {
	sprites, maps, used := map[string]string{}, map[string]map[string]any{}, 0
	uri := func(img image.Image) string {
		var buf bytes.Buffer
		if img == nil || png.Encode(&buf, img) != nil {
			return ""
		}
		src := "data:image/png;base64," + base64.StdEncoding.EncodeToString(buf.Bytes())
		if used+len(src) > pictureBudget {
			return ""
		}
		used += len(src)
		return src
	}
	for _, e := range placed {
		if used >= pictureBudget {
			break
		}
		tileMap, _ := e.Components["TileMapComponent"].(map[string]any)
		if file, _ := tileMap["map"].(string); file != "" && maps[file] == nil {
			if img := tileMapPicture(root, file); img != nil {
				if src := uri(img); src != "" {
					maps[file] = map[string]any{"src": src, "w": img.Bounds().Dx(), "h": img.Bounds().Dy()}
				}
			}
		}
		texture := e.Look
		if _, done := sprites[texture]; texture == "" || done {
			continue
		}
		sprites[texture] = "" // tried: one it can't draw isn't tried again
		sprites[texture] = uri(lookImage(root, texture))
	}
	for texture, src := range sprites {
		if src == "" {
			delete(sprites, texture)
		}
	}
	return sprites, maps
}

// gameImage is an image the engine reads by reference: a file of the game, or
// an atlas's region ("assets/sprites.atlas.json#hero_0") as the build cuts it.
func gameImage(root, ref string) image.Image {
	file, region, ok := strings.Cut(ref, "#")
	if !ok {
		return readGameImage(root, ref)
	}
	var config atlas.AtlasConfig
	if !readGameJSON(root, file, &config) {
		return nil
	}
	img, _ := config.Region(gameReader(root), region, pictureSideMax)
	return img
}
