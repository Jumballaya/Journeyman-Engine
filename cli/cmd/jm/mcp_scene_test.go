package main

import (
	"encoding/json"
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
		"assets/prefabs/cloud.prefab.json": `{"components":{"TransformComponent":{"position":[0,0,0],"scale":[16,8]},"SpriteComponent":{"texture":"cloud"}}}`,
		"scenes/level.scene.json": `{"name":"Level","entities":[
			{"name":"Cloud","prefab":"assets/prefabs/cloud.prefab.json","overrides":{"TransformComponent":{"position":[100,50,0]}}},
			{"name":"Kage","components":{"TransformComponent":{"position":[10,20,0]}},"children":[
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
		got = append(got, jsonOf([]any{e["where"], e["name"], e["x"], e["y"], e["w"], e["h"]}))
	}
	want := `["entities[0]","Cloud",100,50,16,8]|["entities[1]","Kage",10,20,0,0]|["entities[1].children[0]","Light",11,22,0,0]`
	if strings.Join(got, "|") != want {
		t.Errorf("laid out:\n%s\nwant\n%s", strings.Join(got, "|"), want)
	}
	cloud := s["entities"].([]any)[0].(map[string]any)["components"].(map[string]any)
	if jsonOf(cloud) != `{"SpriteComponent":{"texture":"cloud"},"TransformComponent":{"position":[100,50,0],"scale":[16,8]}}` {
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
