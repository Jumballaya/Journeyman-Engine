package main

import (
	"bytes"
	"encoding/base64"
	"image"
	"image/png"
	"os"
	"path"
	"strconv"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
)

// pictureBudget caps the pictures one scene_view sends: past it, sprites and
// maps are drawn as their boxes.
const pictureBudget = 8 << 20

// scenePictures is how the scene's sprites and tile maps look, for the scene
// page: each sprite's texture ("assets/hero.png",
// "assets/sprites.atlas.json#hero_0") as a PNG data URI of the image, or of the
// atlas region as the build cuts it; each tile map's .tmj drawn whole, with its
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
		sprites[texture] = uri(gameImage(root, texture))
	}
	for texture, src := range sprites {
		if src == "" {
			delete(sprites, texture)
		}
	}
	return sprites, maps
}

// spriteLook is the texture a sprite shows as its scene starts: its animation's
// first region when one plays (atlasPath + animations), else its texture.
func spriteLook(components map[string]any) string {
	sprite, _ := components["SpriteComponent"].(map[string]any)
	texture, _ := sprite["texture"].(string)
	anim, _ := components["SpriteAnimationComponent"].(map[string]any)
	file, _ := anim["atlasPath"].(string)
	current, _ := anim["current"].(string)
	clips, _ := anim["animations"].(map[string]any)
	clip, _ := clips[current].(map[string]any)
	regions, _ := clip["regions"].([]any)
	if sprite == nil || file == "" || len(regions) == 0 {
		return texture
	}
	if first, _ := regions[0].(string); first != "" {
		return file + "#" + first
	}
	return texture
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
	return atlasRegion(root, config, region)
}

// atlasRegion is one region of an atlas as the build cuts it: the source
// named for it (a PNG's name, or an Aseprite sheet's name_<frame>); nil when
// no source makes it.
func atlasRegion(root string, config atlas.AtlasConfig, region string) image.Image {
	for _, source := range config.Sources {
		name := strings.TrimSuffix(path.Base(source), path.Ext(source))
		sheet := strings.HasSuffix(source, ".json")
		if !(name == region && !sheet || sheet && strings.HasPrefix(region, name+"_")) {
			continue
		}
		if !sheet {
			return readGameImage(root, source)
		}
		// Only the frame asked for, its sheet's image and its size bounded before they're made.
		i, err := strconv.Atoi(strings.TrimPrefix(region, name+"_"))
		real, ok := gameFile(root, source)
		if err != nil || !ok {
			continue
		}
		data, err := os.ReadFile(real)
		if err != nil {
			continue
		}
		parsed, err := atlas.ParseSheet(data)
		if err != nil {
			continue
		}
		img := readGameImage(root, path.Join(path.Dir(source), parsed.Image))
		if img == nil {
			continue
		}
		if frame, err := parsed.Frame(i, img, pictureSideMax); err == nil {
			return frame
		}
	}
	return nil
}
