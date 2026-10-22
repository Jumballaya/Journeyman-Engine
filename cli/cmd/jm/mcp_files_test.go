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
	"time"
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
	if got := strings.Join(regions, ", "); got != "orb 4×2, secret source \"../secret.png\" isn't a path in the game, kage_0 2×2, kage_1 2×2" {
		t.Errorf("images: %s", got)
	}
	thumbs := r["_meta"].(map[string]any)["jm/files"].(map[string]any)
	if _, ok := thumbs["assets/kage.json#kage_1"]; !ok || len(thumbs) != 3 {
		t.Errorf("thumbs: %v", thumbs)
	}
}

// Only a regular file inside the game is its log: a link out of it, a folder
// or a pipe isn't read (nor waited on); a real older log still is.
func TestGameLogReadsOnlyTheGamesOwnLog(t *testing.T) {
	root := sceneGame(t)
	t.Chdir(root)
	outside := t.TempDir()
	secret := filepath.Join(outside, "engine.log")
	os.WriteFile(secret, []byte("[2026-10-08 09:47:04.080] [engine] [error] SENTINEL\n"), 0o644)
	logOf := func() string {
		r := sceneCall(t, `{"name":"game_log","arguments":{}}`)
		return fmt.Sprint(r["content"], r["structuredContent"])
	}
	log := filepath.Join(root, filepath.FromSlash(engineLog))
	os.Symlink(outside, filepath.Join(root, "logs")) // a linked folder: logs/engine.log is outside
	if got := logOf(); strings.Contains(got, "SENTINEL") || !strings.Contains(got, "No log yet") {
		t.Errorf("a log in a linked folder outside the game was read: %s", got)
	}
	os.Remove(filepath.Join(root, "logs"))
	os.MkdirAll(filepath.Join(root, "logs"), 0o755)
	os.Symlink(secret, log)
	if got := logOf(); strings.Contains(got, "SENTINEL") || !strings.Contains(got, "No log yet") {
		t.Errorf("a linked log outside the game was read: %s", got)
	}
	os.Remove(log)
	os.WriteFile(log, []byte("[2026-10-08 09:47:04.080] [engine] [info] mine\n"), 0o644)
	if got := logOf(); !strings.Contains(got, "mine") {
		t.Errorf("the game's own log: %s", got)
	}
	os.Remove(log)
	os.MkdirAll(log, 0o755) // a folder
	if got := logOf(); !strings.Contains(got, "No log yet") {
		t.Errorf("with a folder there: %s", got)
	}
	os.Remove(log)
	if mkfifo(log) == nil {
		done := make(chan string, 1)
		go func() { done <- logOf() }()
		select {
		case got := <-done:
			if !strings.Contains(got, "No log yet") {
				t.Errorf("with a pipe there: %s", got)
			}
		case <-time.After(5 * time.Second):
			t.Fatal("game_log waited on a pipe")
		}
	}
}

// file_view and game_log answer for the game asked for, open or not, and say which.
func TestFileViewAndLogNameTheirGame(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"A", "B"} {
		os.MkdirAll(filepath.Join(home, g, "assets"), 0o755)
		os.MkdirAll(filepath.Join(home, g, "logs"), 0o755)
		os.WriteFile(filepath.Join(home, g, ".jm.json"), []byte(`{"name":"Same"}`), 0o644)
		os.WriteFile(filepath.Join(home, g, "assets", "input.bindings.json"), []byte(`{"actions":{"jump":["`+g+`"]}}`), 0o644)
		os.WriteFile(filepath.Join(home, g, "logs", "engine.log"), []byte("[2026-10-08 09:47:04.080] [engine] [info] log of "+g+"\n"), 0o644)
	}
	t.Chdir(filepath.Join(home, "A"))
	b := evalOrSelf(filepath.Join(home, "B"))
	r := sceneCall(t, `{"name":"file_view","arguments":{"path":"assets/input.bindings.json","game":`+fmt.Sprintf("%q", b)+`}}`)
	file := r["structuredContent"].(map[string]any)["file"].(map[string]any)
	if file["game"] != b || !strings.Contains(fmt.Sprint(file["actions"]), "B") {
		t.Errorf("B's file: %v", file)
	}
	r = sceneCall(t, `{"name":"game_log","arguments":{"game":`+fmt.Sprintf("%q", b)+`}}`)
	if s := r["structuredContent"].(map[string]any); s["game"] != b || !strings.Contains(fmt.Sprint(s["lines"]), "log of B") {
		t.Errorf("B's log: %v", s)
	}
}
