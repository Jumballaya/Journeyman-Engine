package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// An inspection's entities are placed by where their entries are in the
// file: a child by its index, a prefab's child by its name, nested alike.
func TestAnInspectedEntityIsWhereItsEntryIs(t *testing.T) {
	var seen inspection
	json.Unmarshal([]byte(`{"entities":[
		{"id":[0,0],"name":"Cloud","origin":{"entry":"0","prefab":"cloud.prefab.json"},"components":{"TransformComponent":{"scale":[-8,2]}},
		 "world":{"x":5,"y":6,"z":1,"scaleX":-8,"scaleY":2,"rotation":0.5}},
		{"id":[1,0],"name":"Shadow","origin":{"entry":"0/p0","prefab":"cloud.prefab.json"},"components":{"TransformComponent":{}},"world":{"x":5,"y":9}},
		{"id":[2,0],"name":"Hat","origin":{"entry":"0/p0/c1"},"components":{}},
		{"id":[3,0],"name":"Kage","origin":{"entry":"1"},"components":{}}],
		"draw":{"world":[{"entity":[0,0],"image":"assets/fx.png","texRect":[0,0.5,0.25,0.5]},{"entity":[3,0],"image":"white"}]}}`), &seen)
	var got []string
	for _, p := range seen.placed() {
		got = append(got, jsonOf([]any{p.Where, p.Prefab, p.Inherited, p.X, p.Y, p.W, p.H, p.FlipX, p.Look}))
	}
	want := `["entities[0]","cloud.prefab.json",false,5,6,16,4,true,"assets/fx.png#0,0.5,0.25,0.5"]|` +
		`["entities[0]/Shadow","",true,5,9,0,0,false,""]|["entities[0]/Shadow.children[1]","",true,0,0,0,0,false,""]|` +
		`["entities[1]","",false,0,0,0,0,false,""]`
	if strings.Join(got, "|") != want {
		t.Errorf("placed:\n%s\nwant\n%s", strings.Join(got, "|"), want)
	}
}

// A look's picture is the part of the build's image the engine draws.
func TestALookIsThePartOfItsImageDrawn(t *testing.T) {
	root := t.TempDir()
	writePNG(t, filepath.Join(root, "build", "assets", "fx.png"), 8, 4, color.White)
	if img := lookImage(root, "assets/fx.png#0.5,0,0.25,1"); img == nil || img.Bounds().Size() != (image.Point{2, 4}) {
		t.Errorf("part: %v", img)
	}
	if img := lookImage(root, "assets/fx.png"); img == nil || img.Bounds().Size() != (image.Point{8, 4}) {
		t.Errorf("whole: %v", img)
	}
	if lookImage(root, "assets/none.png") != nil || lookImage(root, "../fx.png") != nil {
		t.Error("an image that isn't the build's")
	}
}

// fakeInspect stands in for the engine where a test is about which file a
// tool shows, not how: a scene's top entries, by name, nowhere.
func fakeInspect(t *testing.T) {
	t.Helper()
	was := inspect
	inspect = func(_ *toolCall, root, file string) (inspection, error) {
		var doc struct{ Entities []struct{ Name string } }
		data, err := os.ReadFile(filepath.Join(root, file))
		if err == nil {
			err = json.Unmarshal(data, &doc)
		}
		var seen inspection
		for i, e := range doc.Entities {
			entity := inspected{Name: e.Name}
			entity.Origin.Entry = fmt.Sprint(i)
			seen.Entities = append(seen.Entities, entity)
		}
		return seen, err
	}
	t.Cleanup(func() { inspect = was })
}

// inEngine runs a test with the engine ($JM_ENGINE, else beside jm), its
// game's build made here (a test can't run jm): skipped where there's none.
func inEngine(t *testing.T) {
	t.Helper()
	if _, err := resolveEnginePath(); err != nil {
		t.Skip("no engine to run (JM_ENGINE):", err)
	}
	was := runJM
	runJM = func(c *toolCall, dir string, args ...string) (string, bool) {
		if len(args) == 0 || args[0] != "build" {
			return was(c, dir, args...)
		}
		wd, _ := os.Getwd()
		os.Chdir(dir)
		defer os.Chdir(wd)
		r := &report{category: "build"}
		err := buildGame(r)
		return "", err != nil || r.errors > 0
	}
	t.Cleanup(func() { runJM = was })
}

// A game whose scene has a prefab instance (overridden, its child too), and
// an entity turned a quarter with a child; its screen 320×180.
func engineGame(t *testing.T, files map[string]string) string {
	t.Helper()
	dir := t.TempDir()
	for path, text := range map[string]string{
		".jm.json": `{"name":"Pip","entryScene":"scenes/level.scene.json","scenes":["scenes/level.scene.json"],
			"assets":["assets/**/*.json","assets/**/*.png"],"config":{"renderer":{"logicalWidth":320,"logicalHeight":180}}}`,
		"assets/prefabs/cloud.prefab.json": `{"components":{"TransformComponent":{"position":[0,0,0],"scale":[16,8]},"SpriteComponent":{"color":[1,0,0,1]}},
			"children":[{"name":"Shadow","components":{"TransformComponent":{"position":[0,4,0]}}}]}`,
		"scenes/level.scene.json": `{"name":"Level","entities":[
			{"name":"Cloud","prefab":"assets/prefabs/cloud.prefab.json","overrides":{"TransformComponent":{"position":[100,50,0]},
				"children":{"Shadow":{"TransformComponent":{"position":[0,6,0]}}}}},
			{"name":"Kage","components":{"TransformComponent":{"position":[10,20,0],"rotation":1.5707963267948966}},"children":[
				{"name":"Light","components":{"TransformComponent":{"position":[1,2,0]}}}]}]}`,
	} {
		if _, given := files[path]; !given {
			files[path] = text
		}
	}
	for path, text := range files {
		os.MkdirAll(filepath.Join(dir, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(dir, path), []byte(text), 0o644)
	}
	return dir
}

// A host's file entrypoint names the file: laid out as the engine builds it.
func TestInEngineSceneViewLaysOutTheFileAHostOpened(t *testing.T) {
	inEngine(t)
	dir := engineGame(t, map[string]string{})
	t.Chdir(t.TempDir()) // no game open: the file says which game
	path, _ := json.Marshal(filepath.Join(dir, "scenes", "level.scene.json"))
	result := sceneCall(t, `{"name":"scene_view","arguments":{"file":{"name":"level.scene.json","resourceUri":"h://1"}},"_meta":{"openai/resource":{"path":`+string(path)+`}}}`)
	s, _ := result["structuredContent"].(map[string]any)
	if s["scene"] != "scenes/level.scene.json" || s["gameName"] != "Pip" || s["name"] != "Level" {
		t.Fatalf("scene: %v", result)
	}
	got := []string{}
	for _, e := range s["entities"].([]any) {
		e := e.(map[string]any)
		round := func(v any) float64 { return math.Round(v.(float64)*1e3) / 1e3 }
		got = append(got, jsonOf([]any{e["where"], e["name"], round(e["x"]), round(e["y"]), e["w"], e["h"], round(e["rotation"])}))
	}
	want := `["entities[0]","Cloud",100,50,32,16,0]|["entities[0]/Shadow","Shadow",100,56,0,0,0]|` +
		`["entities[1]","Kage",10,20,0,0,1.571]|["entities[1].children[0]","Light",8,21,0,0,1.571]`
	if strings.Join(got, "|") != want {
		t.Errorf("laid out:\n%s\nwant\n%s", strings.Join(got, "|"), want)
	}
	cloud := s["entities"].([]any)[0].(map[string]any)["components"].(map[string]any)
	if jsonOf(cloud["TransformComponent"]) != `{"position":[100,50,0],"scale":[16,8]}` || cloud["SpriteComponent"] == nil {
		t.Errorf("the prefab's components under the override: %s", jsonOf(cloud))
	}
	if screen := jsonOf(s["screen"]); screen != `{"camera":0,"h":180,"w":320,"x":0,"y":0}` {
		t.Errorf("screen: %s", screen)
	}
}

// The screen is the engine's camera: a camera entity's, its zoom applied.
func TestInEngineTheScreenIsTheCamerasView(t *testing.T) {
	inEngine(t)
	t.Chdir(engineGame(t, map[string]string{"scenes/level.scene.json": `{"entities":[
		{"name":"Eye","components":{"TransformComponent":{"position":[50,-10,0]},"CameraComponent":{"zoom":2}}}]}`}))
	s := sceneCall(t, `{"name":"scene_view","arguments":{"scene":"scenes/level.scene.json"}}`)["structuredContent"].(map[string]any)
	if screen := jsonOf(s["screen"]); screen != `{"camera":1,"h":90,"w":160,"x":50,"y":-10}` {
		t.Errorf("screen: %s", screen)
	}
}

// Each sprite's picture is what the engine draws it with: its image, an atlas
// region as the build cuts it, an animation's first frame; flipped by its scale.
func TestInEngineSceneViewGivesTheSpritesPictures(t *testing.T) {
	inEngine(t)
	root := engineGame(t, map[string]string{
		"assets/fx/spark.json": `{"frames":[{"frame":{"x":0,"y":0,"w":2,"h":4},"sourceSize":{"w":2,"h":4},"duration":100}],"meta":{"image":"spark.png"}}`,
		"assets/fx.atlas.json": `{"sources":["assets/fx/spark.json"]}`,
		"scenes/level.scene.json": `{"entities":[
			{"name":"Hero","components":{"TransformComponent":{"position":[0,0,2],"scale":[-8,8]},"SpriteComponent":{"texture":"assets/hero.png"}}},
			{"name":"Spark","components":{"TransformComponent":{"scale":[2,3]},"SpriteComponent":{"texture":"assets/fx.atlas.json#spark_0"}}},
			{"name":"Bat","components":{"TransformComponent":{"scale":[2,2]},"SpriteComponent":{},
				"SpriteAnimationComponent":{"atlasPath":"assets/fx.atlas.json","current":"fly","animations":{"fly":{"regions":["spark_0"]}}}}}]}`})
	writePNG(t, filepath.Join(root, "assets", "hero.png"), 16, 16, color.White)
	writePNG(t, filepath.Join(root, "assets", "fx", "spark.png"), 4, 4, color.White)
	t.Chdir(root)
	result := sceneCall(t, `{"name":"scene_view","arguments":{"scene":"scenes/level.scene.json"}}`)
	sprites, _ := result["_meta"].(map[string]any)["jm/sprites"].(map[string]any)
	entities := result["structuredContent"].(map[string]any)["entities"].([]any)
	size := func(i int) image.Point {
		look, _ := entities[i].(map[string]any)["look"].(string)
		uri, _ := sprites[look].(string)
		data, _ := base64.StdEncoding.DecodeString(strings.TrimPrefix(uri, "data:image/png;base64,"))
		img, err := png.Decode(bytes.NewReader(data))
		if err != nil {
			return image.Point{}
		}
		return img.Bounds().Size()
	}
	if size(0) != (image.Point{16, 16}) || size(1) != (image.Point{2, 4}) || size(2) != (image.Point{2, 4}) {
		t.Errorf("pictures %v %v %v: %v", size(0), size(1), size(2), entities)
	}
	if hero := entities[0].(map[string]any); hero["z"] != 2.0 || hero["flipX"] != true || hero["flipY"] != nil {
		t.Errorf("hero: %v", hero)
	}
}

// A prefab's file is one of it as the engine makes it: itself and its
// children, each by its name.
func TestInEngineAPrefabIsShownAsTheEngineMakesIt(t *testing.T) {
	inEngine(t)
	t.Chdir(engineGame(t, map[string]string{}))
	r := sceneCall(t, `{"name":"file_view","arguments":{"path":"assets/prefabs/cloud.prefab.json"}}`)
	file, _ := r["structuredContent"].(map[string]any)["file"].(map[string]any)
	entities, _ := file["entities"].([]any)
	if file["kind"] != "prefab" || len(entities) != 2 {
		t.Fatalf("prefab: %v", r)
	}
	if shadow := entities[1].(map[string]any); shadow["where"] != "cloud/Shadow" || shadow["y"] != 4.0 {
		t.Errorf("its child: %v", shadow)
	}
}
