package atlas

import (
	"bytes"
	"fmt"
	"image"
	"image/png"
	"io/fs"
	"strings"
	"testing"
)

// files is a ReadFunc over a folder held in memory.
type files map[string][]byte

func (f files) read(p string) ([]byte, error) {
	if data, ok := f[p]; ok {
		return data, nil
	}
	return nil, fs.ErrNotExist
}

func pngOf(w, h int) []byte {
	var buf bytes.Buffer
	png.Encode(&buf, image.NewNRGBA(image.Rect(0, 0, w, h)))
	return buf.Bytes()
}

func sheetOf(image string, frames int) []byte {
	var fs []string
	for i := range frames {
		fs = append(fs, fmt.Sprintf(`{"frame":{"x":%d,"y":0,"w":2,"h":2},"spriteSourceSize":{"x":0,"y":0,"w":2,"h":2},"sourceSize":{"w":2,"h":2},"duration":100}`, 2*i))
	}
	return []byte(`{"frames":[` + strings.Join(fs, ",") + `],"meta":{"image":"` + image + `"}}`)
}

func TestASourceIsReadAsTheBuildCutsIt(t *testing.T) {
	game := files{
		"art/orb.png": pngOf(4, 2), "art/orb.normal.png": pngOf(4, 2),
		"art/hero.png": pngOf(4, 2), "art/hero.JSON": sheetOf("hero.png", 2), // a sheet, whatever its case
		"art/big.png": pngOf(64, 64), "art/odd.png": pngOf(2, 2), "art/odd.normal.png": pngOf(1, 1),
	}
	config := AtlasConfig{Sources: []string{"art/orb.png", "art/hero.JSON"}}
	regions, clips, err := config.Read(game.read, 0)
	if err != nil || len(regions) != 3 || regions[0].Normal == nil || regions[1].Name != "hero_0" || regions[2].Normal != nil {
		t.Fatalf("Read = %v, %v", regions, err)
	}
	if len(clips["hero"]["default"].Frames) != 2 {
		t.Errorf("a sheet's clips by its name: %v", clips)
	}
	for _, c := range []struct {
		region string
		ok     bool
	}{{"orb", true}, {"hero_1", true}, {"hero_2", false}, {"hero_01", false}, {"hero", false}, {"orb_0", false}} {
		if img, ok := config.Region(game.read, c.region, 16); ok != c.ok || ok && img.Bounds().Dx() == 0 {
			t.Errorf("Region(%s) = %v", c.region, ok)
		}
	}
	if _, ok := (AtlasConfig{Sources: []string{"art/big.png"}}).Region(game.read, "big", 16); ok {
		t.Error("a picture over side isn't made")
	}
	for sources, want := range map[string]string{
		"art/orb.png art/orb.png": "both produce region name",
		"art/orb.normal.png":      "is a normal map",
		"art/odd.png":             "normal map",
		"art/gone.png":            "file does not exist",
	} {
		if _, _, err := (AtlasConfig{Sources: strings.Fields(sources)}).Read(game.read, 0); err == nil || !strings.Contains(err.Error(), want) {
			t.Errorf("%s: %v, want %q", sources, err, want)
		}
	}
}

func TestASourceIsFoundWhereverItWasWritten(t *testing.T) {
	game := files{"art/hero.png": pngOf(2, 2), "art/hero.json": sheetOf("hero.png", 1), "art/hero_1.png": pngOf(3, 3),
		"art/wide.json": sheetOf("hero.png", 3)}
	config := AtlasConfig{Sources: []string{`art\hero.json`, "art/hero_1.png"}} // written on Windows
	if s := SourceOf(`art\hero.json`); s.Name != "hero" || !s.Sheet {
		t.Errorf("SourceOf a Windows path = %+v", s)
	}
	if img, ok := config.Region(game.read, "hero_1", 16); !ok || img.Bounds().Dx() != 3 {
		t.Error("hero_1 is the picture's: the one-frame sheet hero has no frame 1")
	}
	if _, _, err := (AtlasConfig{Sources: []string{"art/wide.json"}}).Read(game.read, 0); err == nil || !strings.Contains(err.Error(), "art/wide.json") {
		t.Errorf("a frame outside its sheet names the sheet: %v", err)
	}
}
