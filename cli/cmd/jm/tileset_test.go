package main

import (
	"encoding/json"
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"testing"
)

func writePNG(t *testing.T, path string, w, h int, c color.Color) {
	t.Helper()
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.Set(x, y, c)
		}
	}
	mustMkdir(t, filepath.Dir(path))
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if err := png.Encode(f, img); err != nil {
		t.Fatal(err)
	}
}

func mustMkdir(t *testing.T, dir string) {
	t.Helper()
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
}

// An image collection becomes one sheet: each tile a rectangle of it, other fields kept.
func TestBakeTilesetPacksImageCollections(t *testing.T) {
	t.Chdir(t.TempDir())
	red, blue := color.NRGBA{255, 0, 0, 255}, color.NRGBA{0, 0, 255, 255}
	writePNG(t, "assets/textures/grass.png", 16, 16, red)
	writePNG(t, "assets/textures/strip.png", 32, 16, blue)
	mustMkdir(t, "assets/maps")
	mustMkdir(t, filepath.Join(outDir, "assets/maps"))
	src := `{"type": "tileset", "name": "t", "tilewidth": 16, "tileheight": 16, "columns": 0, "tilecount": 2,
	  "tiles": [{"id": 0, "image": "../textures/grass.png", "type": "grass"},
	            {"id": 5, "image": "../textures/strip.png", "x": 16, "y": 0, "width": 16, "height": 16}]}`
	if err := os.WriteFile("assets/maps/t.tsj", []byte(src), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := bakeTileset("assets/maps/t.tsj"); err != nil {
		t.Fatal(err)
	}

	var built struct {
		Name  string `json:"name"`
		Tiles []struct {
			ID                  int    `json:"id"`
			Image, Type         string
			X, Y, Width, Height int
		} `json:"tiles"`
	}
	data, err := os.ReadFile(filepath.Join(outDir, "assets/maps/t.tsj"))
	if err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(data, &built); err != nil {
		t.Fatal(err)
	}
	f, err := os.Open(filepath.Join(outDir, "assets/maps/t.sheet.png"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	sheet, err := png.Decode(f)
	if err != nil {
		t.Fatal(err)
	}
	if built.Name != "t" || len(built.Tiles) != 2 || built.Tiles[0].Type != "grass" || built.Tiles[1].ID != 5 {
		t.Fatalf("tiles not kept: %+v", built)
	}
	for i, want := range []color.NRGBA{red, blue} {
		tile := built.Tiles[i]
		if tile.Image != "t.sheet.png" || tile.Width != 16 || tile.Height != 16 {
			t.Fatalf("tile %d: %+v", i, tile)
		}
		if got := color.NRGBAModel.Convert(sheet.At(tile.X+8, tile.Y+8)); got != want {
			t.Errorf("tile %d samples %v, want %v", i, got, want)
		}
		// The border repeats outward.
		if got := color.NRGBAModel.Convert(sheet.At(tile.X-1, tile.Y-1)); got != want {
			t.Errorf("tile %d corner padding %v, want %v", i, got, want)
		}
	}
}
