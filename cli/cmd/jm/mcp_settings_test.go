package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

func settingsCall(t *testing.T, name string, args map[string]any) toolResult {
	t.Helper()
	for _, tool := range settingsTools() {
		if tool.Name == name {
			return tool.run(args)
		}
	}
	t.Fatalf("no tool %s", name)
	return toolResult{}
}

// ChatGPT finds the settings tools in the capability, and the read tool's
// result fits its SettingsReadResult: a value for every property, layout naming only them.
func TestSettingsAreChatGPTsSettingsPage(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{"name": "Neon Vow", "version": "0.1.0", "entryScene": "scenes/a.scene.json",
		"scenes": ["scenes/a.scene.json", "scenes/b.scene.json"], "config": {"window": {"width": 960}}}`})
	caps := mcpCapabilities()["extensions"].(map[string]any)["openai/settings"].(map[string]any)
	if caps["readTool"] != settingsReadTool || caps["updateTool"] != settingsUpdateTool {
		t.Fatalf("capability: %v", caps)
	}
	read := settingsCall(t, settingsReadTool, map[string]any{}).Structured.(map[string]any)
	props := read["schema"].(map[string]any)["properties"].(map[string]any)
	values := read["values"].(map[string]any)
	for key := range props {
		if _, ok := values[key]; !ok {
			t.Errorf("no value for %s", key)
		}
	}
	if values["config.window.width"] != 960 || values["config.window.height"] != 720 || values["editor.appearance"] != "system" || values["name"] != "Neon Vow" {
		t.Errorf("values (file's, else defaults): %v", values)
	}
	if enum := props["entryScene"].(map[string]any)["enum"]; jsonOf(enum) != `["scenes/a.scene.json","scenes/b.scene.json"]` {
		t.Errorf("start scene choices: %v", enum)
	}
	tools := map[string]bool{}
	for _, tool := range newMCPServer().tools {
		tools[tool.Name] = true
	}
	for _, group := range read["layout"].([]map[string]any) {
		for _, item := range group["items"].([]map[string]any) {
			if item["kind"] == "property" && props[item["property"].(string)] == nil || item["kind"] == "tool" && !tools[item["tool"].(string)] {
				t.Errorf("layout names something that isn't there: %v", item)
			}
		}
	}
}

// An update writes .jm.json in the shared layout (its other keys kept, in
// order) and the editor's preferences as the editor keeps them.
func TestSettingsUpdateWritesTheFilesTheyCameFrom(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": "{\n  \"name\": \"Neon Vow\",\n  \"scenes\": [],\n  \"assets\": [\"assets/**\"]\n}\n"})
	got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{
		"config.window.width": 640.0, "config.window.vsync": false, "editor.appearance": "dark", "editor.exportNotarize": true, "editor.codeEditor": "ask"}})
	if got.Failed {
		t.Fatal(got.Text)
	}
	if v := got.Structured.(map[string]any)["values"].(map[string]any); v["config.window.width"] != 640 || v["editor.exportNotarize"] != true {
		t.Errorf("returned values: %v", v)
	}
	data, _ := os.ReadFile(".jm.json")
	want := "{\n  \"name\": \"Neon Vow\",\n  \"scenes\": [],\n  \"assets\": [\"assets/**\"],\n  \"config\": {\n    \"window\": {\n      \"vsync\": false,\n      \"width\": 640\n    }\n  }\n}\n"
	if string(data) != want {
		t.Errorf(".jm.json:\n%s\nwant:\n%s", data, want)
	}
	var prefs map[string]string
	data, _ = os.ReadFile(projects.PreferencesFile())
	if json.Unmarshal(data, &prefs); prefs["appearance"] != "dark" || prefs["exportNotarize"] != "yes" || prefs["codeEditor"] != "" {
		t.Errorf("preferences: %s", data)
	}
}

// Numbers the update doesn't touch keep their exact literals, however big.
func TestSettingsUpdateKeepsOtherNumbersExactly(t *testing.T) {
	scratchSettings(t)
	file := "{\n  \"name\": \"Neon Vow\",\n  \"customId\": 9007199254740993,\n  \"big\": 18446744073709551615,\n  \"neg\": -9223372036854775808,\n  \"config\": {\n    \"window\": {\n      \"width\": 960\n    }\n  }\n}\n"
	inProject(t, map[string]string{".jm.json": file})
	got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"config.window.width": 640.0}})
	if got.Failed {
		t.Fatal(got.Text)
	}
	if v := got.Structured.(map[string]any)["values"].(map[string]any); v["config.window.width"] != 640 {
		t.Errorf("width read back: %#v", v["config.window.width"])
	}
	data, _ := os.ReadFile(".jm.json")
	if want := strings.Replace(file, "960", "640", 1); string(data) != want {
		t.Errorf(".jm.json:\n%s\nwant:\n%s", data, want)
	}
	if v := settingsCall(t, settingsReadTool, map[string]any{}).Structured.(map[string]any)["values"].(map[string]any); v["config.window.width"] != 640 {
		t.Errorf("width read from the file: %#v", v["config.window.width"])
	}
	os.WriteFile(".jm.json", []byte(`{"name":"X"}}{"keep":1}`), 0o644)
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"config.window.width": 640.0}}); !got.Failed {
		t.Error("a file with more after its object was rewritten")
	}
}

// Updates from several jm at once all land: each holds the files while it changes them.
func TestOverlappingSettingsUpdatesAllLand(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{"name": "Neon Vow"}`})
	sets := []map[string]any{
		{"config.window.width": 640.0}, {"config.window.height": 480.0}, {"config.window.vsync": false}, {"version": "0.2.0"},
		{"editor.appearance": "dark"}, {"editor.codeEditor": "zed"}, {"editor.exportNotarize": true}, {"editor.exportSignIdentity": "Me"},
	}
	var wg sync.WaitGroup
	for _, set := range sets {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": set}); got.Failed {
				t.Error(got.Text)
			}
		}()
	}
	wg.Wait()
	values := settingsCall(t, settingsReadTool, map[string]any{}).Structured.(map[string]any)["values"].(map[string]any)
	for _, set := range sets {
		for key, want := range set {
			if n, ok := want.(float64); ok {
				want = int(n)
			}
			if values[key] != want {
				t.Errorf("%s is %v, want %v", key, values[key], want)
			}
		}
	}
}

// A wrong value changes nothing, and says what would fit; a broken file is an error, not a crash.
func TestSettingsUpdateIsAllOrNothing(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{"name": "Neon Vow"}`})
	for _, set := range []map[string]any{
		{"config.window.width": 10.0}, {"config.window.width": 640.5}, {"name": ""}, {"editor.appearance": "pink"},
		{"config.window.vsync": "no"}, {"nope": 1.0}, {"editor.appearance": "dark", "config.window.height": "tall"},
	} {
		if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": set}); !got.Failed {
			t.Errorf("%v was accepted: %s", set, got.Text)
		}
	}
	if data, _ := os.ReadFile(".jm.json"); string(data) != `{"name": "Neon Vow"}` {
		t.Errorf(".jm.json changed: %s", data)
	}
	if _, err := os.Stat(projects.PreferencesFile()); err == nil {
		t.Error("the preferences were written")
	}
	os.MkdirAll(filepath.Dir(projects.PreferencesFile()), 0o755)
	os.WriteFile(projects.PreferencesFile(), []byte("null"), 0o644)
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"editor.appearance": "dark"}}); !got.Failed {
		t.Errorf("preferences of null: %s", got.Text)
	}
}

// Preferences linked to the open .jm.json can't take a mixed update (one file,
// two saves): it fails untouched, and single-file updates still work.
func TestSettingsUpdateRefusesOneFileInBothRoles(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{"name": "Neon Vow"}`})
	manifest, _ := filepath.Abs(".jm.json")
	os.MkdirAll(filepath.Dir(projects.PreferencesFile()), 0o755)
	if err := os.Symlink(manifest, projects.PreferencesFile()); err != nil {
		t.Skip("no symlinks here:", err)
	}
	done := make(chan toolResult, 1)
	go func() {
		done <- settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"name": "X", "editor.appearance": "dark"}})
	}()
	select {
	case got := <-done:
		if !got.Failed || !strings.Contains(got.Text, "one file") {
			t.Errorf("mixed update: %s", got.Text)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("mixed update hung")
	}
	if data, _ := os.ReadFile(".jm.json"); string(data) != `{"name": "Neon Vow"}` {
		t.Errorf(".jm.json changed: %s", data)
	}
	for _, set := range []map[string]any{{"name": "X"}, {"editor.appearance": "dark"}} {
		if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": set}); got.Failed {
			t.Errorf("%v: %s", set, got.Text)
		}
	}
}

// With no game open there are only the editor's settings, and asking for a game's says to open one.
func TestSettingsWithNoGameOpenAreTheEditors(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{})
	got := settingsCall(t, settingsReadTool, map[string]any{})
	if _, ok := got.Structured.(map[string]any)["values"].(map[string]any)["name"]; ok || !strings.Contains(got.Text, "No game open") {
		t.Errorf("read: %s", got.Text)
	}
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"name": "X"}}); !got.Failed || !strings.Contains(got.Text, "open_game") {
		t.Errorf("update: %s", got.Text)
	}
}

// inProject runs the test in a fresh folder holding files.
func inProject(t *testing.T, files map[string]string) {
	dir := t.TempDir()
	for path, text := range files {
		if err := os.WriteFile(filepath.Join(dir, path), []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	t.Chdir(dir)
}
