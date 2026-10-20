package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestEditorUnsavedReadsALiveSessionOnly(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, ".jm"), 0o755)
	write := func(pid int, file string, age time.Duration) {
		at := time.Now().Add(-age).Unix()
		os.WriteFile(filepath.Join(root, ".jm", fmt.Sprintf("editor-session-%d.json", pid)),
			[]byte(fmt.Sprintf(`{"open": [%q], "unsaved": [%q], "updated": %d}`, file, file, at)), 0o644)
	}
	if got := editorUnsaved(root); got != nil {
		t.Errorf("no session: %v", got)
	}
	write(1, "scenes/a.scene.json", 2*time.Second)
	write(2, "scenes/b.scene.json", 2*time.Second) // a second editor on the same project
	write(3, "scenes/a.scene.json", 2*time.Second) // a third, with the first's file unsaved too
	if got := editorUnsaved(root); strings.Join(got, ",") != "scenes/a.scene.json,scenes/b.scene.json" {
		t.Errorf("live sessions: %v", got)
	}
	write(1, "scenes/a.scene.json", time.Minute) // crashed a minute ago
	write(2, "scenes/b.scene.json", -time.Hour)  // written before the clock went back an hour
	write(3, "scenes/a.scene.json", time.Minute)
	if got := editorUnsaved(root); got != nil {
		t.Errorf("stale sessions: %v", got)
	}
}

func TestEditorUnsavedSkipsTheEditorThatRanIt(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, ".jm"), 0o755)
	now := time.Now().Unix()
	for pid, file := range map[int]string{7: "scenes/mine.scene.json", 8: "scenes/other.scene.json"} {
		os.WriteFile(filepath.Join(root, ".jm", fmt.Sprintf("editor-session-%d.json", pid)),
			[]byte(fmt.Sprintf(`{"unsaved": [%q], "updated": %d}`, file, now)), 0o644)
	}
	t.Setenv("JM_EDITOR_PID", "7")
	if got := editorUnsaved(root); strings.Join(got, ",") != "scenes/other.scene.json" {
		t.Errorf("an editor's own build: %v", got)
	}
}

// The agent reads what the person has open and pointed out, and shows them
// things through the editor's inbox.
func TestTheEditorBridgeReadsTheSessionAndWritesItsInbox(t *testing.T) {
	root := sceneGame(t)
	t.Chdir(root)
	session := filepath.Join(root, ".jm", "editor-session-42.json")
	os.MkdirAll(filepath.Dir(session), 0o755)
	os.WriteFile(session, []byte(fmt.Sprintf(`{"open": ["scenes/level.scene.json"], "unsaved": [], "scene": "scenes/level.scene.json",
		"selection": ["Player"], "notes": [{"id": 3, "at": %d, "scene": "scenes/level.scene.json", "entities": ["Player"], "text": "jumps too low"}],
		"updated": %d}`, time.Now().Unix(), time.Now().Unix())), 0o644)
	state := sceneCall(t, `{"name":"editor_state","arguments":{}}`)
	text := state["content"].([]any)[0].(map[string]any)["text"].(string)
	if !strings.Contains(text, "scenes/level.scene.json open, Player selected") || !strings.Contains(text, "about Player in scenes/level.scene.json): jumps too low") {
		t.Errorf("editor_state: %s", text)
	}
	if pid := state["structuredContent"].(map[string]any)["editors"].([]any)[0].(map[string]any)["pid"]; pid != "42" {
		t.Errorf("pid: %v", pid)
	}
	shown := sceneCall(t, `{"name":"editor_show","arguments":{"scene":"scenes/level.scene.json","entity":"Player","note":"this one"}}`)
	if shown["isError"] == true {
		t.Fatalf("editor_show: %v", shown)
	}
	inbox, _ := os.ReadFile(filepath.Join(root, ".jm", "editor-session-42.in"))
	if string(inbox) != `{"entity":"Player","note":"this one","scene":"scenes/level.scene.json"}`+"\n" {
		t.Errorf("inbox: %q", inbox)
	}
	if r := sceneCall(t, `{"name":"editor_show","arguments":{"scene":"../elsewhere.scene.json"}}`); r["isError"] != true {
		t.Errorf("a scene outside the game: %v", r)
	}
}

// Studio says which game its tab shows: another chat having opened another
// game since doesn't send its editor, index, health or scene calls there.
func TestTheEditorAndSceneToolsUseTheGameNamed(t *testing.T) {
	root := sceneGame(t)
	session := filepath.Join(root, ".jm", "editor-session-7.json")
	os.MkdirAll(filepath.Dir(session), 0o755)
	os.WriteFile(session, []byte(fmt.Sprintf(`{"open": [], "unsaved": [], "scene": "scenes/level.scene.json", "selection": [], "notes": [], "updated": %d}`, time.Now().Unix())), 0o644)
	other := t.TempDir()
	os.WriteFile(filepath.Join(other, ".jm.json"), []byte(`{"name":"Other"}`), 0o644)
	t.Chdir(other) // the server's open game
	game, _ := json.Marshal(root)
	state := sceneCall(t, `{"name":"editor_state","arguments":{"game":`+string(game)+`}}`)
	if editors := state["structuredContent"].(map[string]any)["editors"].([]any); len(editors) != 1 {
		t.Errorf("editor_state: %v", state)
	}
	if r := sceneCall(t, `{"name":"editor_show","arguments":{"game":`+string(game)+`,"scene":"scenes/level.scene.json"}}`); r["isError"] == true {
		t.Errorf("editor_show: %v", r)
	}
	if inbox, _ := os.ReadFile(filepath.Join(root, ".jm", "editor-session-7.in")); !strings.Contains(string(inbox), "scenes/level.scene.json") {
		t.Errorf("inbox: %q", inbox)
	}
	if r := sceneCall(t, `{"name":"scene_view","arguments":{"game":`+string(game)+`,"scene":"scenes/level.scene.json"}}`); r["isError"] == true {
		t.Errorf("scene_view: %v", r)
	}
	if r := sceneCall(t, `{"name":"editor_show","arguments":{"game":`+string(game)+`,"scene":"scenes/none.scene.json"}}`); r["isError"] != true {
		t.Errorf("editor_show showed a missing scene: %v", r)
	}
	link := filepath.Join(t.TempDir(), "link")
	os.Symlink(root, link)
	for _, g := range []string{root, link} {
		name, _ := json.Marshal(g)
		index := sceneCall(t, `{"name":"game_index","arguments":{"game":`+string(name)+`}}`)
		if entries, _ := index["structuredContent"].(map[string]any)["entries"].([]any); len(entries) == 0 {
			t.Errorf("game_index of %s: %v", g, index)
		}
		if r := sceneCall(t, `{"name":"game_index","arguments":{"game":`+string(name)+`,"path":"scenes/level.scene.json"}}`); r["isError"] == true {
			t.Errorf("game_index path of %s: %v", g, r)
		}
	}
	os.MkdirAll(filepath.Join(root, "design"), 0o755)
	os.WriteFile(filepath.Join(root, "design", "Art.md"), []byte("# Art\n"), 0o644)
	health := sceneCall(t, `{"name":"health","arguments":{"game":`+string(game)+`,"checks":["art"]}}`)
	if text := fmt.Sprint(health["structuredContent"]); !strings.Contains(text, "Art.md") {
		t.Errorf("health read another game's art: %v", health)
	}
	// The open game itself has no editor and no such scene.
	if r := sceneCall(t, `{"name":"scene_view","arguments":{"scene":"scenes/level.scene.json"}}`); r["isError"] != true {
		t.Errorf("the open game's scene_view found the other's scene: %v", r)
	}
}
