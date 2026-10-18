package main

import (
	"image"
	"image/color"
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
	labeled := 0 // under the crosshair: no room above it
	for x := 470; x < 540; x++ {
		for y := 22; y < 36; y++ {
			if ink(x, y) {
				labeled++
			}
		}
	}
	if labeled == 0 {
		t.Error("the point's label isn't under it")
	}
	label := 0
	for x := 158; x < 260; x++ {
		for y := 70; y < 86; y++ {
			if ink(x, y) {
				label++
			}
		}
	}
	if label == 0 {
		t.Error("no label above the pick")
	}
	note := 0 // its note, white, under its name
	for x := 158; x < 260; x++ {
		for y := 76; y < 88; y++ {
			if r, g, b, _ := img.At(x, y).RGBA(); r&g&b>>8 == 0xFF {
				note++
			}
		}
	}
	if note == 0 {
		t.Error("no note under the pick's name")
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
		!strings.HasSuffix(link["description"].(string), `picked Pip: "this one clips", enemy_zero, point: "this gap is too wide"`) || link["uri"] != "jm://play/2000-01-03_000000/m1" {
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

// A label near the game's right edge stays in the game, off the letterbox
// bars (a game with a dark top row is still pillarboxed); one wider than the
// game is cut at its edge; a long note is cut to its lines.
func TestALabelStaysInTheGame(t *testing.T) {
	black := color.RGBA{0, 0, 0, 0xFF}
	img := image.NewRGBA(image.Rect(0, 0, 640, 360))
	fill(img, img.Bounds(), black)
	fill(img, image.Rect(80, 1, 560, 360), color.RGBA{0x40, 0x60, 0x90, 0xFF})
	game := gameArea(img)
	if game != image.Rect(80, 0, 560, 360) {
		t.Fatalf("game area: %v", game)
	}
	lines := noteLines(2, plays.Pick{Kind: "point", Note: "this gap is too wide, and the next one too: jump it twice and see"})
	if len(lines) != 3 || lines[0] != "2 point" || !strings.HasSuffix(lines[2], "…") {
		t.Errorf("lines: %q", lines)
	}
	narrow := image.Rect(240, 0, 400, 360) // a game narrower than the label
	for _, g := range []image.Rectangle{game, narrow} {
		pic := image.NewRGBA(img.Bounds())
		fill(pic, pic.Bounds(), black)
		label(pic.SubImage(g).(*image.RGBA), tagPlace(g, image.Rect(g.Max.X-20, 100, g.Max.X-10, 110), labelSize(lines, 2), nil), lines, 2)
		inked := false
		for y := 0; y < 360; y++ {
			for x := 0; x < 640; x++ {
				r, gr, b, _ := pic.At(x, y).RGBA()
				if !(image.Pt(x, y).In(g)) && r|gr|b != 0 {
					t.Fatalf("drawn on the bar at (%d, %d) of %v", x, y, g)
				}
				inked = inked || r>>8 == 0xFF
			}
		}
		if !inked {
			t.Errorf("no label in %v", g)
		}
	}
}
