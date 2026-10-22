package main

import (
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// A play with a moment: marker 1 picked Pip (a scene entry), an enemy a
// script spawned and a point on a map's tile, each where it was in the
// picture, Pip and the point with a note; marker 2 is plain.
func momentGame(t *testing.T) *plays.Play {
	t.Helper()
	t.Chdir(t.TempDir())
	os.WriteFile(".jm.json", []byte(`{}`), 0o644)
	dir := filepath.Join(plays.Root("."), "2000-01-03_000000")
	os.MkdirAll(dir, 0o755)
	os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":3,"frames":300,"seconds":5,"started":"2000-01-03T00:00:00","markers":[
		{"n":1,"frame":120,"time":2,"scene":"scenes/level.scene.json","note":"this one","image":"m1.png","picks":[
			{"kind":"entity","entity":[58,0],"tags":["Pip","player"],"state":{},"rect":[0.25,0.25,0.5,0.5],"note":"this one clips",
			 "origin":{"from":"scene","scene":"scenes/level.scene.json","entry":"58","name":"Pip","prefab":"prefabs/pip.prefab.json"}},
			{"kind":"entity","entity":[122,1],"tags":[],"state":{},
			 "origin":{"from":"spawn","prefab":"prefabs/enemy_zero.prefab.json","frame":40,"by":[93,1],"script":"scripts/director.ts"}},
			{"kind":"point","at":[200,40],"scene":"scenes/level.scene.json","rect":[0.75,0.01,0,0],"entities":[],"note":"this gap is too wide",
			 "tiles":[{"map":"maps/level.tmj","entity":[0,0],"from":[12,2],"to":[12,2],"layers":[{"layer":"ground","cells":[[12,2,5]],"count":1}]}]}]},
		{"n":2,"frame":200,"time":3.3,"scene":"scenes/level.scene.json","image":"m2.png"}]}`), 0o644)
	writePNG(t, filepath.Join(dir, "m1.png"), 640, 360, color.Black)
	writePNG(t, filepath.Join(dir, "m2.png"), 640, 360, color.Black)
	p, err := plays.Find(".", "latest")
	if err != nil {
		t.Fatal(err)
	}
	return p
}

// The moment's picture outlines each pick where it was (one without a rect
// isn't drawn), and leaves the rest of the frame as it was.
func TestAMomentsPictureOutlinesItsPicks(t *testing.T) {
	p := momentGame(t)
	img := momentPicture(p, p.Meta.Markers[0])
	if img == nil {
		t.Fatal("no picture")
	}
	ink := func(x, y int) bool {
		r, g, b, _ := img.At(x, y).RGBA()
		return r>>8 == 0xFF && g>>8 == 0xD2 && b>>8 == 0x3F
	}
	// The rect is 160..480 x 90..270; the outline goes around it.
	for _, at := range [][2]int{{158, 180}, {481, 180}, {320, 88}, {320, 271}} {
		if !ink(at[0], at[1]) {
			t.Errorf("no outline at %v: %v", at, img.At(at[0], at[1]))
		}
	}
	if ink(320, 180) || ink(10, 340) {
		t.Error("drawn inside the pick or elsewhere")
	}
	// The point at (480, 3): a crosshair, its arms 16 px out.
	for _, at := range [][2]int{{494, 3}, {466, 3}, {480, 18}} {
		if !ink(at[0], at[1]) {
			t.Errorf("no crosshair at %v", at)
		}
	}
	inked := func(r image.Rectangle, white bool) bool {
		for y := r.Min.Y; y < r.Max.Y; y++ {
			for x := r.Min.X; x < r.Max.X; x++ {
				if rr, g, b, _ := img.At(x, y).RGBA(); white && rr&g&b>>8 == 0xFF || !white && ink(x, y) {
					return true
				}
			}
		}
		return false
	}
	// Each number at its pick: on Pip's outline's top-left corner, above it;
	// under the point's crosshair, left of its arm (no room above it).
	if !inked(image.Rect(158, 74, 168, 88), false) {
		t.Error("no number on Pip's corner")
	}
	if !inked(image.Rect(464, 10, 474, 24), false) {
		t.Error("no number by the point")
	}
	// The legend, bottom left: each pick's number, name and note (white).
	if !inked(image.Rect(0, 334, 240, 360), true) {
		t.Error("no legend in the corner")
	}
	if plain := momentPicture(p, p.Meta.Markers[1]); plain == nil {
		t.Error("a plain marker has no picture")
	} else if r, g, b, _ := plain.At(320, 88).RGBA(); r|g|b != 0 {
		t.Errorf("a plain marker's picture changed: %v", plain.At(320, 88))
	}
}

// What the agent gets of a moment: play_moment's picture and picks (each
// with its file), play_show's marker with them, a mention, the listing.
func TestAMomentReachesTheAgentWithItsPicks(t *testing.T) {
	momentGame(t)
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"play_moment","arguments":{"marker":"m1"}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"mention_search","arguments":{"query":"moment"}}}`,
		`{"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"jm://play/2000-01-03_000000/m1"}}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"play_moment","arguments":{"marker":"m9"}}}`)
	result := replies[1]["result"].(map[string]any)
	content := result["content"].([]any)
	text := content[0].(map[string]any)["text"].(string)
	for _, want := range []string{
		`Marker 1 of play 2000-01-03_000000 at 0:02.0 (frame 120), scenes/level.scene.json: "this one"`,
		`picked 1: Pip [58 0]: scene entry 58 in scenes/level.scene.json (prefab prefabs/pip.prefab.json): "this one clips"`,
		"picked 2: entity [122 1]: spawned from prefab prefabs/enemy_zero.prefab.json by script scripts/director.ts at frame 40",
		`picked 3: a point at (200, 40) in scenes/level.scene.json, over tile (12, 2) of maps/level.tmj: layer ground gid 5: "this gap is too wide"`,
	} {
		if !strings.Contains(text, want) {
			t.Errorf("play_moment says:\n%s\nwant %q", text, want)
		}
	}
	if len(content) != 2 || content[1].(map[string]any)["type"] != "image" || result["_meta"].(map[string]any)["jm/image"] == nil {
		t.Errorf("play_moment's picture: %v", content[1:])
	}
	if picked := result["structuredContent"].(map[string]any)["picked"].([]any); len(picked) != 3 {
		t.Errorf("picked: %v", picked)
	}
	items := replies[2]["result"].(map[string]any)["structuredContent"].(map[string]any)["items"].([]any)
	if link := items[0].(map[string]any); link["name"] != "moment 1: this one" ||
		!strings.HasSuffix(link["description"].(string), `picked Pip: "this one clips", enemy_zero, point: "this gap is too wide"`) || link["uri"] != gameURI(evalOrSelf(here(t)), playLink("2000-01-03_000000", 1)) {
		t.Errorf("mention: %v", items)
	}
	read := replies[3]["result"].(map[string]any)["contents"].([]any)
	if len(read) != 2 || !strings.Contains(read[0].(map[string]any)["text"].(string), "picked 2: entity [122 1]") {
		t.Errorf("mention read: %v", read)
	}
	if replies[4]["result"].(map[string]any)["isError"] != true {
		t.Errorf("no marker 9: %v", replies[4]["result"])
	}
	listing, err := listPlays(".")
	if err != nil || len(listing.Plays) != 1 || jsonOf(listing.Plays[0].Moments) != "[1]" ||
		listing.Plays[0].Marks[0] != `m1 0:02.0: this one · picked Pip: "this one clips", enemy_zero, point: "this gap is too wide"` {
		t.Errorf("listing: %+v %v", listing, err)
	}
}

// A page names the game it shows: its moment is that game's, whichever is open.
func TestAMomentIsReadFromTheGameNamed(t *testing.T) {
	momentGame(t)
	game, _ := os.Getwd()
	t.Chdir(t.TempDir())
	os.WriteFile(".jm.json", []byte(`{}`), 0o644) // another game, open, with no plays
	replies := mcpSession(t, `{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"play_moment","arguments":{"game":"`+game+`","marker":"1"}}}`)
	result := replies[1]["result"].(map[string]any)
	if result["isError"] == true || result["structuredContent"].(map[string]any)["play"] != "2000-01-03_000000" {
		t.Errorf("play_moment in %s: %v", game, result)
	}
}

// A moment's picture of frame, its marker given a game area and a point
// (frame px) with a note.
func pictureOf(t *testing.T, frame *image.RGBA, game []float64, point image.Point) image.Image {
	t.Helper()
	p := momentGame(t)
	m := p.Meta.Markers[0]
	m.Game = game
	m.Picks = []plays.Pick{{Kind: "point", Rect: []float64{float64(point.X) / 640, float64(point.Y) / 360, 0, 0}, Note: "too wide"}}
	f, _ := os.Create(markerImage(p, m))
	png.Encode(f, frame)
	f.Close()
	return momentPicture(p, m)
}

// Where the picture has the outlines' color (white: the legend's text) in r.
func inkedIn(img image.Image, r image.Rectangle, white bool) bool {
	for y := r.Min.Y; y < r.Max.Y; y++ {
		for x := r.Min.X; x < r.Max.X; x++ {
			rr, g, b, _ := img.At(x, y).RGBA()
			if white && rr&g&b>>8 == 0xFF || !white && rr>>8 == 0xFF && g>>8 == 0xD2 && b>>8 == 0x3F {
				return true
			}
		}
	}
	return false
}

// The numbers and the legend stay in the game the engine saved, off the
// letterbox bars, each number by its pick at the game's edge (a game with a
// dark top row is still just its game).
func TestTheLegendStaysInTheGame(t *testing.T) {
	black := color.RGBA{0, 0, 0, 0xFF}
	for _, c := range []struct {
		name string
		game []float64
		area image.Rectangle
	}{
		{"pillarboxed", []float64{0.125, 0, 0.75, 1}, image.Rect(80, 0, 560, 360)},
		{"letterboxed", []float64{0, 0.1, 1, 0.8}, image.Rect(0, 36, 640, 324)},
	} {
		t.Run(c.name, func(t *testing.T) {
			frame := image.NewRGBA(image.Rect(0, 0, 640, 360))
			fill(frame, frame.Bounds(), black)
			fill(frame, c.area, color.RGBA{0x40, 0x60, 0x90, 0xFF})
			fill(frame, image.Rect(c.area.Min.X, c.area.Min.Y, c.area.Max.X, c.area.Min.Y+1), black) // the game's own dark row
			at := image.Pt(c.area.Max.X-40, c.area.Min.Y+3)                                          // near its top-right corner
			pic := pictureOf(t, frame, c.game, at)
			legend := false
			for y := 0; y < 360; y++ {
				for x := 0; x < 640; x++ {
					r, g, b, _ := pic.At(x, y).RGBA()
					if !image.Pt(x, y).In(c.area) && r|g|b != 0 {
						t.Fatalf("drawn on the bar at (%d, %d)", x, y)
					}
					legend = legend || y > c.area.Max.Y-26 && r&g&b>>8 == 0xFF
				}
			}
			if !legend {
				t.Error("no legend at the game's bottom")
			}
			// Under the crosshair, left of its arm: no room above it in the game.
			if !inkedIn(pic, image.Rect(at.X-16, at.Y+7, at.X-6, at.Y+21), false) {
				t.Error("no number by the point")
			}
		})
	}
}

// A play from before the engine saved the game's area: all of the picture
// is the game, even one of one color at its edges (a sky) with its art in
// the middle; the number stays by its pick and the legend is readable.
func TestAnOlderMomentIsAllGame(t *testing.T) {
	frame := image.NewRGBA(image.Rect(0, 0, 640, 360))
	fill(frame, frame.Bounds(), color.RGBA{0x40, 0x60, 0x90, 0xFF})
	fill(frame, image.Rect(310, 170, 330, 190), color.RGBA{0xC0, 0x30, 0x30, 0xFF})
	pic := pictureOf(t, frame, nil, image.Pt(160, 90))
	if !inkedIn(pic, image.Rect(144, 70, 154, 84), false) {
		t.Error("no number up-left of the point")
	}
	if !inkedIn(pic, image.Rect(60, 334, 200, 360), true) {
		t.Error("no legend along the frame's bottom")
	}
}

// The saved game area comes back to the pixel in a big frame (a 7000 px
// wide one, the game 997 px at 3001), and an older moment's is all of it.
func TestTheGameAreaIsThePixelsSaved(t *testing.T) {
	frame := image.Rect(0, 0, 7000, 4000)
	if game := gameArea(plays.Marker{Game: []float64{0.428714, 0, 0.142429, 1}}, frame); game != image.Rect(3001, 0, 3998, 4000) {
		t.Errorf("game area: %v", game)
	}
	if game := gameArea(plays.Marker{}, frame); game != frame {
		t.Errorf("an older moment's game area: %v", game)
	}
}
