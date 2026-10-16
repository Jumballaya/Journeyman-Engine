package main

import (
	"bytes"
	"fmt"
	"image"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// A prefab is laid out with its children, key bindings listed; other files are refused.
func TestFileViewShowsPrefabsAndBindings(t *testing.T) {
	root := sceneGame(t)
	t.Chdir(root)
	os.WriteFile(filepath.Join(root, "assets", "input.bindings.json"), []byte(`{"actions":{"jump":["Space","Gamepad.A"]}}`), 0o644)
	r := sceneCall(t, `{"name":"file_view","arguments":{"path":"assets/prefabs/cloud.prefab.json"}}`)
	file := r["structuredContent"].(map[string]any)["file"].(map[string]any)
	entities := file["entities"].([]any)
	if file["kind"] != "prefab" || len(entities) != 2 || entities[1].(map[string]any)["name"] != "Shadow" {
		t.Errorf("prefab: %v", file)
	}
	if _, ok := r["structuredContent"].(map[string]any)["games"]; !ok {
		t.Error("the Studio around it has no games")
	}
	r = sceneCall(t, `{"name":"file_view","arguments":{"path":"assets/input.bindings.json"}}`)
	if text := r["content"].([]any)[0].(map[string]any)["text"].(string); !strings.Contains(text, "jump: Space, Gamepad.A") {
		t.Errorf("bindings: %s", text)
	}
	if r := sceneCall(t, `{"name":"file_view","arguments":{"path":"scenes/level.scene.json"}}`); r["isError"] != true {
		t.Errorf("a scene: %v", r)
	}
}

// The game's log: its last lines with their levels, the newest run's.
func TestGameLogReadsTheNewestLog(t *testing.T) {
	root := sceneGame(t)
	t.Chdir(root)
	os.MkdirAll(filepath.Join(root, "logs"), 0o755)
	os.WriteFile(filepath.Join(root, "logs", "engine.log"), []byte("[2026-10-08 09:47:04.080] [engine] [info] started\n"+
		"[2026-10-08 09:47:04.081] [engine] [warning] [Inputs] skipping\n[2026-10-08 09:47:04.082] [engine] [error] [script] no field\n"), 0o644)
	r := sceneCall(t, `{"name":"game_log","arguments":{"errors":true}}`)
	lines := r["structuredContent"].(map[string]any)["lines"].([]any)
	if len(lines) != 2 || lines[1].(map[string]any)["level"] != "error" || lines[1].(map[string]any)["text"] != "[script] no field" {
		t.Errorf("lines: %v", lines)
	}
	if text := r["content"].([]any)[0].(map[string]any)["text"].(string); !strings.HasPrefix(text, "logs/engine.log: 1 errors, 1 warnings") {
		t.Errorf("text: %s", text)
	}
}

// An atlas shows its images by region; a source outside the game isn't read.
func TestFileViewShowsAnAtlasFromTheGameOnly(t *testing.T) {
	root := sceneGame(t)
	t.Chdir(root)
	img := image.NewNRGBA(image.Rect(0, 0, 4, 2))
	var buf bytes.Buffer
	png.Encode(&buf, img)
	os.WriteFile(filepath.Join(root, "assets", "orb.png"), buf.Bytes(), 0o644)
	os.WriteFile(filepath.Join(filepath.Dir(root), "secret.png"), buf.Bytes(), 0o644)
	os.WriteFile(filepath.Join(root, "assets", "kage.png"), buf.Bytes(), 0o644)
	frame := func(x int) string {
		return fmt.Sprintf(`{"frame":{"x":%d,"y":0,"w":2,"h":2},"rotated":false,"trimmed":false,"spriteSourceSize":{"x":0,"y":0,"w":2,"h":2},"sourceSize":{"w":2,"h":2},"duration":100}`, x)
	}
	os.WriteFile(filepath.Join(root, "assets", "kage.json"), []byte(`{"frames":[`+frame(0)+`,`+frame(2)+`],"meta":{"image":"kage.png","size":{"w":4,"h":2}}}`), 0o644)
	os.WriteFile(filepath.Join(root, "assets", "fx.atlas.json"), []byte(`{"sources":["assets/orb.png","../secret.png","assets/kage.json"]}`), 0o644)
	r := sceneCall(t, `{"name":"file_view","arguments":{"path":"assets/fx.atlas.json"}}`)
	images := r["structuredContent"].(map[string]any)["file"].(map[string]any)["images"].([]any)
	regions := []string{}
	for _, i := range images {
		regions = append(regions, i.(map[string]any)["region"].(string)+" "+i.(map[string]any)["size"].(string))
	}
	if got := strings.Join(regions, ", "); got != "orb 4×2, secret not a file of the game, kage_0 2×2, kage_1 2×2" {
		t.Errorf("images: %s", got)
	}
	thumbs := r["_meta"].(map[string]any)["jm/files"].(map[string]any)
	if _, ok := thumbs["assets/kage.json#kage_1"]; !ok || len(thumbs) != 3 {
		t.Errorf("thumbs: %v", thumbs)
	}
}
