package atlas

import (
	"image"
	"image/color"
	"reflect"
	"strings"
	"testing"
)

// Four 2x2 frames in a row; frame 2 trimmed to its right column. Hash frames, out of name order.
const sheetJSON = `{
  "frames": {
    "kage 3.aseprite": {"frame": {"x": 6, "y": 0, "w": 2, "h": 2}, "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 2, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 100},
    "kage 0.aseprite": {"frame": {"x": 0, "y": 0, "w": 2, "h": 2}, "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 2, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 100},
    "kage 1.aseprite": {"frame": {"x": 2, "y": 0, "w": 2, "h": 2}, "rotated": false, "trimmed": true,
      "spriteSourceSize": {"x": 1, "y": 0, "w": 1, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 250},
    "kage 2.aseprite": {"frame": {"x": 4, "y": 0, "w": 2, "h": 2}, "rotated": false, "trimmed": false,
      "spriteSourceSize": {"x": 0, "y": 0, "w": 2, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 50}
  },
  "meta": {"app": "https://www.aseprite.org/", "image": "kage.png", "size": {"w": 8, "h": 2},
    "frameTags": [
      {"name": "idle", "from": 0, "to": 0, "direction": "forward"},
      {"name": "run", "from": 0, "to": 3, "direction": "pingpong"},
      {"name": "back", "from": 1, "to": 2, "direction": "reverse", "repeat": "2"}
    ]}
}`

func sheetImage() *image.NRGBA {
	img := image.NewNRGBA(image.Rect(0, 0, 8, 2))
	for x := range 8 {
		for y := range 2 {
			img.Set(x, y, color.NRGBA{uint8(x * 10), uint8(y), 0, 255})
		}
	}
	return img
}

func TestAsepriteHashFramesKeepTheFilesOrder(t *testing.T) {
	sheet, err := ParseSheet([]byte(sheetJSON))
	if err != nil {
		t.Fatal(err)
	}
	if sheet.Image != "kage.png" {
		t.Fatalf("image %q", sheet.Image)
	}
	sources, _, err := sheet.Cut("kage", sheetImage(), nil)
	if err != nil {
		t.Fatal(err)
	}
	// The hash lists frame 3 first: it's region kage_0, from x 6.
	if got := sources[0].Img.At(0, 0).(color.NRGBA).R; got != 60 {
		t.Fatalf("kage_0 starts at x %d, want the file's first frame (60)", got)
	}
}

func TestAsepriteTagsBecomeClipsWithTheirDirectionsAndDurations(t *testing.T) {
	sheet, _ := ParseSheet([]byte(`{"frames": [` +
		`{"frame": {"x": 0, "y": 0, "w": 2, "h": 2}, "spriteSourceSize": {"x": 0, "y": 0, "w": 2, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 100},` +
		`{"frame": {"x": 2, "y": 0, "w": 2, "h": 2}, "spriteSourceSize": {"x": 0, "y": 0, "w": 2, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 250},` +
		`{"frame": {"x": 4, "y": 0, "w": 2, "h": 2}, "spriteSourceSize": {"x": 0, "y": 0, "w": 2, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 50},` +
		`{"frame": {"x": 6, "y": 0, "w": 2, "h": 2}, "spriteSourceSize": {"x": 0, "y": 0, "w": 2, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 100}],` +
		`"meta": {"image": "kage.png", "frameTags": [` +
		`{"name": "run", "from": 0, "to": 3, "direction": "pingpong"},` +
		`{"name": "back", "from": 1, "to": 2, "direction": "reverse", "repeat": "2"}]}}`))
	_, clips, err := sheet.Cut("kage", sheetImage(), nil)
	if err != nil {
		t.Fatal(err)
	}
	regions := func(c Clip) (out []string) {
		for _, f := range c.Frames {
			out = append(out, f.Region)
		}
		return out
	}
	if got := regions(clips["run"]); !reflect.DeepEqual(got, []string{"kage_0", "kage_1", "kage_2", "kage_3", "kage_2", "kage_1"}) || !clips["run"].Loop {
		t.Fatalf("run: %v loop %v", got, clips["run"].Loop)
	}
	if clips["run"].Frames[1].Duration != 0.25 {
		t.Fatalf("frame durations are per frame, in seconds: %v", clips["run"].Frames[1].Duration)
	}
	if got := regions(clips["back"]); !reflect.DeepEqual(got, []string{"kage_2", "kage_1", "kage_2", "kage_1"}) || clips["back"].Loop {
		t.Fatalf("back (reversed, twice, then stops): %v loop %v", got, clips["back"].Loop)
	}
}

func TestATrimmedFrameKeepsItsPlaceAndAnUntaggedSheetIsOneClip(t *testing.T) {
	sheet, _ := ParseSheet([]byte(`{"frames": [{"frame": {"x": 2, "y": 0, "w": 1, "h": 2}, "trimmed": true,` +
		`"spriteSourceSize": {"x": 1, "y": 0, "w": 1, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 100}],` +
		`"meta": {"image": "kage.png"}}`))
	sources, clips, err := sheet.Cut("kage", sheetImage(), nil)
	if err != nil {
		t.Fatal(err)
	}
	img := sources[0].Img
	if img.Bounds().Dx() != 2 || img.At(0, 0).(color.NRGBA).A != 0 || img.At(1, 0).(color.NRGBA).R != 20 {
		t.Fatalf("trimmed frame: size %v, left %v, right %v", img.Bounds().Size(), img.At(0, 0), img.At(1, 0))
	}
	if len(clips) != 1 || len(clips["default"].Frames) != 1 {
		t.Fatalf("clips %v", clips)
	}
}

func TestBadSheetsSayWhy(t *testing.T) {
	for _, doc := range []string{
		`{"frames": [], "meta": {}}`,
		`{"frames": [{"frame": {"w": 1, "h": 1}, "rotated": true}], "meta": {"image": "a.png"}}`,
		`{"frames": [], "meta": {"image": "a.png", "frameTags": [{"name": "x", "from": 0, "to": 3}]}}`,
	} {
		if _, err := ParseSheet([]byte(doc)); err == nil {
			t.Errorf("no error for %s", doc)
		}
	}
	sheet, _ := ParseSheet([]byte(`{"frames": [{"frame": {"x": 7, "y": 0, "w": 4, "h": 2}}], "meta": {"image": "a.png"}}`))
	if _, _, err := sheet.Cut("a", sheetImage(), nil); err == nil {
		t.Error("a frame outside the sheet cut without an error")
	}
}

func TestSheetsWithoutFramesAreRejected(t *testing.T) {
	for _, frames := range []string{"null", "true", "0", `"frame"`, "[]", "{}"} {
		t.Run(frames, func(t *testing.T) {
			doc := `{"frames":` + frames + `,"meta":{"image":"hero.png"}}`
			if _, err := ParseSheet([]byte(doc)); err == nil || !strings.Contains(err.Error(), "no frames") {
				t.Fatalf("expected no frames error, got %v", err)
			}
		})
	}
}

func TestAFinitePingPongCountsEachWayAsAPass(t *testing.T) {
	got := playOrder(sheetTag{From: 0, To: 2, Direction: "pingpong", Repeat: "2"})
	if !reflect.DeepEqual(got, []int{0, 1, 2, 1, 0}) {
		t.Fatalf("got %v", got)
	}
}

func TestInnerPaddingIsNotPartOfTheFrame(t *testing.T) {
	// A 2x2 sprite exported with inner padding 1: its frame rect is 4x4.
	sheet, _ := ParseSheet([]byte(`{"frames": [{"frame": {"x": 2, "y": 0, "w": 4, "h": 2},` +
		`"spriteSourceSize": {"x": 0, "y": 0, "w": 2, "h": 2}, "sourceSize": {"w": 2, "h": 2}, "duration": 100}],` +
		`"meta": {"image": "a.png"}}`))
	sources, _, err := sheet.Cut("a", sheetImage(), nil)
	if err != nil {
		t.Fatal(err)
	}
	if img := sources[0].Img; img.Bounds().Dx() != 2 || img.At(0, 0).(color.NRGBA).R != 30 {
		t.Fatalf("size %v, first pixel %v (want the sheet's x 3)", img.Bounds().Size(), img.At(0, 0))
	}
}
