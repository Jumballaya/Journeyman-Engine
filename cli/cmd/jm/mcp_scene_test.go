package main

import (
	"encoding/json"
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
