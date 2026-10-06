package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"image"
	"image/draw"
	"image/png"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
)

// bakeTileset packs a Tiled image-collection tileset's tile images (each
// tile its own PNG, as Tiled and the editor author them) into one sheet next
// to it in the build, rewriting each tile to a rectangle of the sheet
// (Tiled 1.9+'s x/y/width/height), so the game binds one texture per tileset.
// Spritesheet tilesets are already one image and stay as copied.
func bakeTileset(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var ts map[string]any
	if err := json.Unmarshal(data, &ts); err != nil {
		return fmt.Errorf("parse: %w", err)
	}
	tiles, _ := ts["tiles"].([]any)
	if ts["image"] != nil || len(tiles) == 0 {
		return nil
	}

	var sources []atlas.SourceImage
	owners := map[string]map[string]any{} // source name → its tile
	for i, raw := range tiles {
		tile, _ := raw.(map[string]any)
		file, _ := tile["image"].(string)
		if file == "" {
			continue
		}
		img, err := decodeTileImage(filepath.Join(filepath.Dir(path), filepath.FromSlash(file)), tile)
		if err != nil {
			return err
		}
		name := strconv.Itoa(i)
		sources = append(sources, atlas.SourceImage{Name: name, Img: img})
		owners[name] = tile
	}
	if len(sources) == 0 {
		return nil
	}
	const padding = 2 // each region's edge pixels are repeated into it, so filtering never samples a neighbour
	sheet, rects, err := atlas.Pack(sources, padding, 4096)
	if err != nil {
		return err
	}
	for _, r := range rects {
		extrude(sheet, r)
	}

	sheetRel := strings.TrimSuffix(path, ".tsj") + ".sheet.png"
	var encoded bytes.Buffer
	if err := png.Encode(&encoded, sheet); err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(outDir, sheetRel), encoded.Bytes(), 0o644); err != nil {
		return err
	}
	for name, tile := range owners {
		r := rects[name]
		tile["image"] = filepath.Base(sheetRel)
		tile["imagewidth"], tile["imageheight"] = sheet.Bounds().Dx(), sheet.Bounds().Dy()
		tile["x"], tile["y"], tile["width"], tile["height"] = r[0], r[1], r[2], r[3]
	}
	out, err := json.Marshal(ts)
	if err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(outDir, path), out, 0o644); err != nil {
		return err
	}
	fmt.Printf("Tileset: %s (%d tiles on one %dx%d sheet)\n", path, len(sources), sheet.Bounds().Dx(), sheet.Bounds().Dy())
	return nil
}

// decodeTileImage reads a tile's PNG, cut to its x/y/width/height if it has them.
func decodeTileImage(file string, tile map[string]any) (image.Image, error) {
	f, err := os.Open(file)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		return nil, fmt.Errorf("decode %s: %w", file, err)
	}
	w, hasW := tile["width"].(float64)
	h, hasH := tile["height"].(float64)
	if !hasW || !hasH {
		return img, nil
	}
	x, _ := tile["x"].(float64)
	y, _ := tile["y"].(float64)
	at := img.Bounds().Min.Add(image.Pt(int(x), int(y)))
	cut := image.NewNRGBA(image.Rect(0, 0, int(w), int(h)))
	draw.Draw(cut, cut.Bounds(), img, at, draw.Src)
	return cut, nil
}

// extrude copies a region's border pixels one pixel outward.
func extrude(img *image.NRGBA, r [4]int) {
	x0, y0, x1, y1 := r[0], r[1], r[0]+r[2]-1, r[1]+r[3]-1
	b := img.Bounds()
	set := func(x, y, fromX, fromY int) {
		if image.Pt(x, y).In(b) {
			img.Set(x, y, img.At(fromX, fromY))
		}
	}
	for x := x0; x <= x1; x++ {
		set(x, y0-1, x, y0)
		set(x, y1+1, x, y1)
	}
	for y := y0 - 1; y <= y1+1; y++ {
		cy := min(max(y, y0), y1)
		set(x0-1, y, x0, cy)
		set(x1+1, y, x1, cy)
	}
}
