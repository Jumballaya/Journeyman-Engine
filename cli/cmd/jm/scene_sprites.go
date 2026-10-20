package main

import (
	"bytes"
	"encoding/base64"
	"image"
	"image/png"
	"path"
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
		if img == nil || png.Encode(&buf, img) != nil || used+buf.Len()*4/3 > pictureBudget {
			return ""
		}
		used += buf.Len() * 4 / 3
		return "data:image/png;base64," + base64.StdEncoding.EncodeToString(buf.Bytes())
	}
	atlases := map[string]atlas.AtlasConfig{} // atlas file → its sources
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
		sprite, _ := e.Components["SpriteComponent"].(map[string]any)
		texture, _ := sprite["texture"].(string)
		if _, done := sprites[texture]; texture == "" || done {
			continue
		}
		sprites[texture] = "" // tried: one it can't draw isn't tried again
		var img image.Image
		if file, region, ok := strings.Cut(texture, "#"); ok {
			config, read := atlases[file]
			if !read {
				readGameJSON(root, file, &config)
				atlases[file] = config
			}
			img = atlasRegion(root, config, region)
		} else {
			img = readGameImage(root, texture)
		}
		sprites[texture] = uri(img)
	}
	for texture, src := range sprites {
		if src == "" {
			delete(sprites, texture)
		}
	}
	return sprites, maps
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
		frames, _ := atlasSource(root, source, name)
		for _, f := range frames {
			if f.Name == region {
				return f.Img
			}
		}
	}
	return nil
}
