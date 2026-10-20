package main

import (
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"strings"
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

// A host's file entrypoint names the file in the call's _meta: laid out with its prefab.
func TestSceneViewLaysOutTheFileAHostOpened(t *testing.T) {
	dir := sceneGame(t)
	t.Chdir(t.TempDir()) // no game open: the file says which game
	path, _ := json.Marshal(filepath.Join(dir, "scenes", "level.scene.json"))
	result := sceneCall(t, `{"name":"scene_view","arguments":{"file":{"name":"level.scene.json","resourceUri":"h://1"}},"_meta":{"openai/resource":{"path":`+string(path)+`}}}`)
	s := result["structuredContent"].(map[string]any)
	if s["scene"] != "scenes/level.scene.json" || s["game"] != "Pip" {
		t.Fatalf("scene: %v", result)
	}
	got := []string{}
	for _, e := range s["entities"].([]any) {
		e := e.(map[string]any)
		got = append(got, jsonOf([]any{e["where"], e["name"], math.Round(e["x"].(float64)*1e6) / 1e6, math.Round(e["y"].(float64)*1e6) / 1e6, e["w"], e["h"]}))
	}
	want := `["entities[0]","Cloud",100,50,32,16]|["entities[0]/Shadow","Shadow",100,56,0,0]|["entities[1]","Kage",10,20,0,0]|["entities[1].children[0]","Light",8,21,0,0]`
	if strings.Join(got, "|") != want {
		t.Errorf("laid out:\n%s\nwant\n%s", strings.Join(got, "|"), want)
	}
	for _, i := range []int{2, 3} { // Kage turned a quarter, and Light with it
		if e := s["entities"].([]any)[i].(map[string]any); e["rotation"] != math.Pi/2 {
			t.Errorf("%v turned %v, want π/2", e["name"], e["rotation"])
		}
	}
	cloud := s["entities"].([]any)[0].(map[string]any)["components"].(map[string]any)
	if jsonOf(cloud) != `{"ScriptComponent":{"params":{"hp":3,"speed":5}},"TransformComponent":{"position":[100,50,0],"scale":[16,8]}}` {
		t.Errorf("the prefab's components under the override: %s", jsonOf(cloud))
	}
}

func TestSceneViewOfTheOpenGame(t *testing.T) {
	t.Chdir(sceneGame(t))
	if result := sceneCall(t, `{"name":"scene_view","arguments":{"scene":"scenes/level.scene.json"}}`); result["isError"] == true {
		t.Errorf("by name: %v", result)
	}
	// The model can't name a file outside the game, nor pose as a host's file.
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

// A parent's change to a child is a merge patch, as the engine's: null takes the child's own override away.
func TestANullChildOverrideRestoresTheNestedPrefabsDefault(t *testing.T) {
	dir := t.TempDir()
	for path, text := range map[string]string{
		".jm.json":                         `{"name":"Pip"}`,
		"assets/prefabs/inner.prefab.json": `{"components":{"TransformComponent":{"position":[2,3,0]}}}`,
		"assets/prefabs/outer.prefab.json": `{"components":{},"children":[{"name":"Inner","prefab":"assets/prefabs/inner.prefab.json",
			"overrides":{"TransformComponent":{"position":[5,5,0]}}}]}`,
		"scenes/level.scene.json": `{"entities":[{"name":"Outer","prefab":"assets/prefabs/outer.prefab.json",
			"overrides":{"children":{"Inner":{"TransformComponent":{"position":null}}}}}]}`,
	} {
		os.MkdirAll(filepath.Join(dir, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(dir, path), []byte(text), 0o644)
	}
	t.Chdir(dir)
	s := sceneCall(t, `{"name":"scene_view","arguments":{"scene":"scenes/level.scene.json"}}`)["structuredContent"].(map[string]any)
	inner := s["entities"].([]any)[1].(map[string]any)
	if inner["x"] != 2.0 || inner["y"] != 3.0 {
		t.Errorf("Inner at (%v, %v), want the inner prefab's (2, 3)", inner["x"], inner["y"])
	}
}

// As the engine's TransformHierarchy: a child is placed from its parent only when that has a transform.
func TestAParentWithoutATransformPlacesNothing(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, ".jm.json"), []byte(`{"name":"Pip"}`), 0o644)
	os.MkdirAll(filepath.Join(dir, "scenes"), 0o755)
	os.WriteFile(filepath.Join(dir, "scenes", "level.scene.json"), []byte(`{"entities":[
		{"name":"P","components":{"TransformComponent":{"position":[100,0,0]}},"children":[
			{"name":"N","components":{},"children":[{"name":"G","components":{"TransformComponent":{"position":[2,0,0]}}}]}]}]}`), 0o644)
	t.Chdir(dir)
	s := sceneCall(t, `{"name":"scene_view","arguments":{"scene":"scenes/level.scene.json"}}`)["structuredContent"].(map[string]any)
	g := s["entities"].([]any)[2].(map[string]any)
	if g["name"] != "G" || g["x"] != 2.0 {
		t.Errorf("%v at x %v, want G at its own 2", g["name"], g["x"])
	}
}

func TestSceneRenderTakesOnlyAScene(t *testing.T) {
	t.Chdir(sceneGame(t))
	for _, scene := range []string{".jm.json", "../other.scene.json", "scenes/none.scene.json"} {
		if r := sceneCall(t, `{"name":"scene_render","arguments":{"scene":"`+scene+`"}}`); r["isError"] != true {
			t.Errorf("%s: %v", scene, r)
		}
	}
}

// The screen a scene starts on: the logical size (else the window's) about the
// first camera, its zoom applied, else about the origin.
func TestSceneScreenIsTheGamesView(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{"config":{"window":{"width":800,"height":600},"renderer":{"logicalWidth":320,"logicalHeight":180}}}`), 0o644)
	if s, ok := screenOf(root, nil); !ok || s["w"] != 320 || s["h"] != 180 || s["x"] != 0 {
		t.Errorf("logical: %v %v", s, ok)
	}
	at := map[string]any{"position": []any{}}
	cam := placedEntity{X: 50, Y: -10, Components: map[string]any{"CameraComponent": map[string]any{"zoom": 2.0}, "TransformComponent": at}}
	if s, _ := screenOf(root, []placedEntity{{}, cam}); s["x"] != 50 || s["y"] != -10 || s["w"] != 160 || s["h"] != 90 {
		t.Errorf("a camera's: %v", s)
	}
	// The highest priority camera is shown, on the entity it follows.
	follow := placedEntity{X: 9, Components: map[string]any{"CameraComponent": map[string]any{"priority": 1.0, "target": "Kage"}, "TransformComponent": at}}
	unplaced := placedEntity{Components: map[string]any{"CameraComponent": map[string]any{"priority": 5.0}}} // no transform: no camera to the engine
	kage := placedEntity{Name: "Kage", X: 300, Y: 40, Components: map[string]any{"TransformComponent": at}}
	if s, _ := screenOf(root, []placedEntity{cam, follow, unplaced, kage}); s["x"] != 300 || s["y"] != 40 || s["w"] != 320 {
		t.Errorf("the shown camera's: %v", s)
	}
	// The first of that name only, and only if it's placed: else where the camera is.
	if s, _ := screenOf(root, []placedEntity{follow, {Name: "Kage", X: 7}, kage}); s["x"] != 9 || s["y"] != 0 {
		t.Errorf("an unplaced target: %v", s)
	}
	if s, _ := screenOf(root, []placedEntity{follow}); s["x"] != 9 {
		t.Errorf("no target: %v", s)
	}
	// Kept inside the first region the target is in, else the bounds; a box narrower than the view: its middle.
	rect := func(x0, y0, x1, y1 float64) map[string]any {
		return map[string]any{"min": []any{x0, y0}, "max": []any{x1, y1}}
	}
	bounded := func(regions ...any) placedEntity {
		return placedEntity{Components: map[string]any{"TransformComponent": at, "CameraComponent": map[string]any{"target": "Kage",
			"bounds": rect(0, -100, 1000, 100), "regions": regions}}}
	}
	for _, c := range []struct {
		cam  placedEntity
		x, y float64
	}{
		{bounded(), 300, 10}, // 300 ± 160 is inside; 40 + 90 is past 100
		{bounded(rect(250, 0, 600, 200), rect(0, 0, 1000, 200)), 410, 90}, // the first region it's in
		{bounded(rect(500, 0, 600, 100)), 300, 10},                        // in none: the bounds
		{bounded(rect(250, 0, 350, 100)), 300, 50},                        // narrower than the view: its middle
	} {
		if s, _ := screenOf(root, []placedEntity{c.cam, kage}); s["x"] != c.x || s["y"] != c.y {
			t.Errorf("bounded %v: %v, want %v, %v", c.cam.Components["CameraComponent"], s, c.x, c.y)
		}
	}
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{"config":{"window":{"width":800,"height":600},"renderer":{"logicalWidth":320}}}`), 0o644)
	if s, _ := screenOf(root, nil); s["w"] != 320 || s["h"] != 600 {
		t.Errorf("by axis: %v", s)
	}
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{}`), 0o644)
	if _, ok := screenOf(root, nil); ok {
		t.Error("no size, a screen")
	}
}

// The page draws each sprite as it looks: its image, or its atlas region as
// the build cuts it; a texture that isn't there is left to its box.
func TestSceneViewGivesTheSpritesPictures(t *testing.T) {
	root := sceneGame(t)
	square := func(file string, w, h int) {
		img := image.NewNRGBA(image.Rect(0, 0, w, h))
		var buf bytes.Buffer
		png.Encode(&buf, img)
		os.MkdirAll(filepath.Join(root, filepath.Dir(file)), 0o755)
		os.WriteFile(filepath.Join(root, file), buf.Bytes(), 0o644)
	}
	square("assets/hero.png", 16, 16)
	square("assets/fx/spark_0.png", 4, 6)
	os.WriteFile(filepath.Join(root, "assets/fx.atlas.json"), []byte(`{"sources":["assets/fx/spark_0.png"]}`), 0o644)
	os.WriteFile(filepath.Join(root, "scenes/art.scene.json"), []byte(`{"name":"Art","entities":[
		{"name":"Hero","components":{"TransformComponent":{"position":[0,0,2],"scale":[-8,8]},"SpriteComponent":{"texture":"assets/hero.png"}}},
		{"name":"Spark","components":{"TransformComponent":{"scale":[2,3]},"SpriteComponent":{"texture":"assets/fx.atlas.json#spark_0"}}},
		{"name":"Gone","components":{"TransformComponent":{"scale":[2,2]},"SpriteComponent":{"texture":"assets/none.png"}}}]}`), 0o644)
	t.Chdir(root)
	result := sceneCall(t, `{"name":"scene_view","arguments":{"scene":"scenes/art.scene.json"}}`)
	sprites, _ := result["_meta"].(map[string]any)["jm/sprites"].(map[string]any)
	size := func(texture string) image.Point {
		uri, _ := sprites[texture].(string)
		data, _ := base64.StdEncoding.DecodeString(strings.TrimPrefix(uri, "data:image/png;base64,"))
		img, err := png.Decode(bytes.NewReader(data))
		if err != nil {
			return image.Point{}
		}
		return img.Bounds().Size()
	}
	if size("assets/hero.png") != (image.Point{16, 16}) || size("assets/fx.atlas.json#spark_0") != (image.Point{4, 6}) || len(sprites) != 2 {
		t.Errorf("sprites: %v", sprites)
	}
	hero := result["structuredContent"].(map[string]any)["entities"].([]any)[0].(map[string]any)
	if hero["z"] != 2.0 || hero["flipX"] != true || hero["flipY"] != nil {
		t.Errorf("hero: %v", hero)
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
	// Sizes past what's sent (or past what an int holds) are left to the box, not allocated.
	for _, size := range []string{`"width":4096,"height":4096`, `"width":4294967296,"height":4294967296`} {
		os.WriteFile(filepath.Join(root, "maps", "c.tmj"), []byte(`{`+size+`,"tilewidth":2,"tileheight":2,"layers":[]}`), 0o644)
		if tileMapPicture(root, "maps/c.tmj") != nil {
			t.Errorf("%s: drawn", size)
		}
	}
}
