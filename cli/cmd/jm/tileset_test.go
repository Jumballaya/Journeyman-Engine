package main

import (
	"encoding/json"
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"slices"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
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

// A source's x.normal.png packs into a parallel image the built atlas names; normals aren't regions.
func TestBakeAtlasPacksNormalMaps(t *testing.T) {
	t.Chdir(t.TempDir())
	writePNG(t, "art/kage.png", 8, 8, color.NRGBA{255, 0, 0, 255})
	writePNG(t, "art/kage.normal.png", 8, 8, color.NRGBA{255, 128, 128, 255})
	writePNG(t, "art/sentry.png", 8, 8, color.NRGBA{0, 0, 255, 255})
	mustMkdir(t, "assets")
	mustMkdir(t, filepath.Join(outDir, "assets"))
	cfg := `{"sources": ["art/kage.png", "art/sentry.png"]}`
	if err := os.WriteFile("assets/c.atlas.json", []byte(cfg), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := bakeAtlas("assets/c.atlas.json"); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(filepath.Join(outDir, "assets/c.atlas.json"))
	if err != nil {
		t.Fatal(err)
	}
	var built atlas.AtlasOutput
	if err := json.Unmarshal(data, &built); err != nil {
		t.Fatal(err)
	}
	if built.NormalImage != "assets/c.atlas.normal.png" || len(built.Regions) != 2 {
		t.Fatalf("built: %+v", built)
	}
	normals, err := readPNG(filepath.Join(outDir, built.NormalImage))
	if err != nil {
		t.Fatal(err)
	}
	k, s := built.Regions["kage"], built.Regions["sentry"]
	if r, g, _, _ := normals.At(k[0], k[1]).RGBA(); r>>8 != 255 || g>>8 != 128 {
		t.Errorf("kage's normal: got %v", normals.At(k[0], k[1]))
	}
	if _, _, b, _ := normals.At(s[0], s[1]).RGBA(); b>>8 != 255 {
		t.Errorf("sentry's normal: want flat, got %v", normals.At(s[0], s[1]))
	}

	if err := os.WriteFile("assets/c.atlas.json", []byte(`{"sources": ["art/kage.normal.png"]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := bakeAtlas("assets/c.atlas.json"); err == nil {
		t.Error("a normal map listed as a source: want an error")
	}
}

// Loose images ship their normal maps unlisted: the engine loads them by name.
func TestWithNormalMapsAddsSiblings(t *testing.T) {
	t.Chdir(t.TempDir())
	writePNG(t, "assets/a.png", 1, 1, color.White)
	writePNG(t, "assets/a.normal.png", 1, 1, color.White)
	writePNG(t, "assets/b.png", 1, 1, color.White)
	got := withNormalMaps([]string{"assets/a.png", "assets/b.png"})
	if want := []string{"assets/a.png", "assets/b.png", "assets/a.normal.png"}; !slices.Equal(got, want) {
		t.Errorf("want %v, got %v", want, got)
	}
	if got := withNormalMaps([]string{"assets/a.png", "assets/a.normal.png"}); len(got) != 2 {
		t.Errorf("listed already: got %v", got)
	}
}
