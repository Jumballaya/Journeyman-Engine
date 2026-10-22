package main

import (
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"testing"
)

// A scene with a prefab instance (one override), a plain entity and a child.
func sceneGame(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	for path, text := range map[string]string{
		".jm.json": `{"name":"Pip"}`,
		"assets/prefabs/cloud.prefab.json": `{"components":{"TransformComponent":{"position":[0,0,0],"scale":[16,8]},"ScriptComponent":{"params":{"hp":3,"speed":2}}},
			"children":[{"name":"Shadow","components":{"TransformComponent":{"position":[0,4,0]}}}]}`,
		"scenes/level.scene.json": `{"name":"Level","entities":[
			{"name":"Cloud","prefab":"assets/prefabs/cloud.prefab.json","overrides":{"TransformComponent":{"position":[100,50,0]},
				"ScriptComponent":{"params":{"speed":5}},"children":{"Shadow":{"TransformComponent":{"position":[0,6,0]}}}}},
			{"name":"Kage","components":{"TransformComponent":{"position":[10,20,0],"rotation":1.5707963267948966}},"children":[
				{"name":"Light","components":{"TransformComponent":{"position":[1,2,0]}}}]}]}`,
	} {
		os.MkdirAll(filepath.Join(dir, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(dir, path), []byte(text), 0o644)
	}
	return dir
}

func sceneCall(t *testing.T, call string) map[string]any {
	t.Helper()
	return mcpSession(t, `{"jsonrpc":"2.0","id":1,"method":"tools/call","params":`+call+`}`)[1]["result"].(map[string]any)
}

// The model can't name a file outside the game, nor pose as a host's file.
func TestSceneViewTakesOnlyTheGamesScenes(t *testing.T) {
	t.Chdir(sceneGame(t))
	for _, call := range []string{`{"name":"scene_view","arguments":{"scene":"/etc/x.scene.json"}}`,
		`{"name":"scene_view","arguments":{"openai/resource":"/etc/x.scene.json"}}`,
		`{"name":"scene_view","arguments":{"scene":".jm.json"}}`} {
		if result := sceneCall(t, call); result["isError"] != true {
			t.Errorf("%s: %v", call, result)
		}
	}
}

func TestSceneFilesOpenInTheScenePage(t *testing.T) {
	for _, tool := range newMCPServer().tools {
		if tool.Name == "scene_view" {
			if got := jsonOf(tool.Meta["openai/ui"]); got != `{"entrypoints":[{"extensions":[".scene.json"],"type":"file"}]}` {
				t.Errorf("entrypoint: %s", got)
			}
			return
		}
	}
	t.Error("no scene_view")
}

func TestSceneRenderTakesOnlyAScene(t *testing.T) {
	t.Chdir(sceneGame(t))
	for _, scene := range []string{".jm.json", "../other.scene.json", "scenes/none.scene.json"} {
		if r := sceneCall(t, `{"name":"scene_render","arguments":{"scene":"`+scene+`"}}`); r["isError"] != true {
			t.Errorf("%s: %v", scene, r)
		}
	}
}

// A tile map is drawn as the engine draws it: a sheet tileset (external, beside
// the map) cut into its grid, gids placed row 0 on top, a flipped tile mirrored.
func TestATileMapIsDrawnWhole(t *testing.T) {
	root := t.TempDir()
	sheet := image.NewNRGBA(image.Rect(0, 0, 4, 2)) // two 2×2 tiles: red, then green with a blue left column
	for y := range 2 {
		sheet.Set(0, y, color.NRGBA{255, 0, 0, 255})
		sheet.Set(1, y, color.NRGBA{255, 0, 0, 255})
		sheet.Set(2, y, color.NRGBA{0, 0, 255, 255})
		sheet.Set(3, y, color.NRGBA{0, 255, 0, 255})
	}
	var buf bytes.Buffer
	png.Encode(&buf, sheet)
	os.MkdirAll(filepath.Join(root, "maps"), 0o755)
	os.WriteFile(filepath.Join(root, "maps", "tiles.png"), buf.Bytes(), 0o644)
	os.WriteFile(filepath.Join(root, "maps", "set.tsj"), []byte(`{"tilewidth":2,"tileheight":2,"image":"tiles.png","columns":2,"tilecount":2}`), 0o644)
	// Row 0: red, green flipped; row 1: nothing, green.
	os.WriteFile(filepath.Join(root, "maps", "a.tmj"), []byte(`{"width":2,"height":2,"tilewidth":2,"tileheight":2,
		"tilesets":[{"firstgid":1,"source":"set.tsj"}],"layers":[{"type":"tilelayer","data":[1,2147483650,0,2]}]}`), 0o644)
	img := tileMapPicture(root, "maps/a.tmj")
	if img == nil || img.Bounds().Size() != (image.Point{4, 4}) {
		t.Fatalf("picture: %v", img)
	}
	at := func(x, y int) color.NRGBA { return color.NRGBAModel.Convert(img.At(x, y)).(color.NRGBA) }
	red, green, blue := color.NRGBA{255, 0, 0, 255}, color.NRGBA{0, 255, 0, 255}, color.NRGBA{0, 0, 255, 255}
	if at(0, 0) != red || at(2, 0) != green || at(3, 0) != blue || at(0, 2).A != 0 || at(2, 2) != blue || at(3, 2) != green {
		t.Errorf("pixels: %v %v %v %v %v %v", at(0, 0), at(2, 0), at(3, 0), at(0, 2), at(2, 2), at(3, 2))
	}

	// The same layer as base64 gids, the green tile turned across its diagonal: its blue column becomes its top row.
	gids := make([]byte, 16)
	for i, gid := range []uint32{1, 2 | 0x20000000, 0, 2} {
		binary.LittleEndian.PutUint32(gids[4*i:], gid)
	}
	os.WriteFile(filepath.Join(root, "maps", "b.tmj"), []byte(`{"width":2,"height":2,"tilewidth":2,"tileheight":2,
		"tilesets":[{"firstgid":1,"source":"set.tsj"}],"layers":[{"type":"tilelayer","encoding":"base64","data":"`+base64.StdEncoding.EncodeToString(gids)+`"}]}`), 0o644)
	if img = tileMapPicture(root, "maps/b.tmj"); img == nil || at(0, 0) != red || at(2, 0) != blue || at(3, 0) != blue || at(2, 1) != green || at(3, 1) != green {
		t.Errorf("base64, diagonal: %v", img)
	}
	// A layer's "z" orders it, as the engine's: the later red layer, below, is under the green.
	os.WriteFile(filepath.Join(root, "maps", "z.tmj"), []byte(`{"width":1,"height":1,"tilewidth":2,"tileheight":2,
		"tilesets":[{"firstgid":1,"source":"set.tsj"}],"layers":[{"type":"tilelayer","data":[2],"properties":[{"name":"z","type":"float","value":1}]},
		{"type":"tilelayer","data":[1],"properties":[{"name":"z","type":"float","value":-1}]}]}`), 0o644)
	if img = tileMapPicture(root, "maps/z.tmj"); img == nil || at(1, 0) != green {
		t.Errorf("layer z: %v", img)
	}
	// An animated tile shows its first frame; a hidden group's layers still take their z steps.
	os.WriteFile(filepath.Join(root, "maps", "anim.tsj"), []byte(`{"tilewidth":2,"tileheight":2,"image":"tiles.png","columns":2,"tilecount":2,
		"tiles":[{"id":0,"animation":[{"tileid":1,"duration":100}]}]}`), 0o644)
	os.WriteFile(filepath.Join(root, "maps", "h.tmj"), []byte(`{"width":1,"height":1,"tilewidth":2,"tileheight":2,
		"tilesets":[{"firstgid":1,"source":"anim.tsj"}],"layers":[{"type":"group","visible":false,"layers":[{"type":"tilelayer","data":[2]}]},
		{"type":"tilelayer","data":[1]},{"type":"tilelayer","data":[0],"properties":[{"name":"z","value":0.005}]}]}`), 0o644)
	if img = tileMapPicture(root, "maps/h.tmj"); img == nil || at(1, 0) != green {
		t.Errorf("animated tile: %v", img)
	}
	// A tile's image may be an atlas region, as the engine reads it.
	os.WriteFile(filepath.Join(root, "maps", "art.atlas.json"), []byte(`{"sources":["maps/tiles.png"]}`), 0o644)
	os.WriteFile(filepath.Join(root, "maps", "atlas.tmj"), []byte(`{"width":1,"height":1,"tilewidth":4,"tileheight":2,
		"tilesets":[{"firstgid":1,"tilewidth":4,"tileheight":2,"tilecount":1,"tiles":[{"id":0,"image":"art.atlas.json#tiles"}]}],
		"layers":[{"type":"tilelayer","data":[1]}]}`), 0o644)
	if img = tileMapPicture(root, "maps/atlas.tmj"); img == nil || at(0, 0) != red || at(3, 0) != green {
		t.Errorf("atlas tile: %v", img)
	}
	// Offsets add up before they're rounded: a group's 0.4 and its layer's 0.4 are the 0.8 of one layer.
	for _, layers := range []string{`[{"type":"group","offsetx":0.4,"offsety":-0.4,"layers":[{"type":"tilelayer","offsetx":0.4,"offsety":-0.4,"data":[1]}]}]`,
		`[{"type":"tilelayer","offsetx":0.8,"offsety":-0.8,"data":[1]}]`} {
		os.WriteFile(filepath.Join(root, "maps", "o.tmj"), []byte(`{"width":2,"height":2,"tilewidth":2,"tileheight":2,
			"tilesets":[{"firstgid":1,"source":"set.tsj"}],"layers":`+layers+`}`), 0o644)
		if img = tileMapPicture(root, "maps/o.tmj"); img == nil || at(0, 0).A != 0 || at(1, 0) != red || at(2, 0) != red || at(3, 0).A != 0 {
			t.Errorf("offset %s: %v %v %v", layers, at(0, 0), at(2, 0), at(3, 0))
		}
	}
	// Sizes past what's sent (or past what an int holds) are left to the box, not allocated.
	for _, size := range []string{`"width":4096,"height":4096`, `"width":4294967296,"height":4294967296`} {
		os.WriteFile(filepath.Join(root, "maps", "c.tmj"), []byte(`{`+size+`,"tilewidth":2,"tileheight":2,"layers":[]}`), 0o644)
		if tileMapPicture(root, "maps/c.tmj") != nil {
			t.Errorf("%s: drawn", size)
		}
	}
}
