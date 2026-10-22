package main

import (
	"encoding/json"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/plugin"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

func settingsCall(t *testing.T, name string, args map[string]any) toolResult {
	t.Helper()
	return toolOf(t, newMCPServer(), name).run(args) // a server started here, as hosts start one
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

// Agent settings are preferences.json's agent object, as JSON values beside
// the editor's strings, with or without a game open; unset or of the wrong
// type, they're their defaults.
func TestAgentSettingsLiveInPreferences(t *testing.T) {
	scratchSettings(t)
	inProject(t, nil)
	prefsFile := projects.PreferencesFile()
	os.MkdirAll(filepath.Dir(prefsFile), 0o755)
	os.WriteFile(prefsFile, []byte(`{"appearance": "dark", "agent": {"recordDecisions": "no", "experiments": "nowhere", "mine": 1}}`), 0o644)
	values := settingsCall(t, settingsReadTool, map[string]any{}).Structured.(map[string]any)["values"].(map[string]any)
	if values["agent.commitMilestones"] != true || values["agent.experiments"] != "sandbox" || values["agent.recordDecisions"] != true || values["agent.askBeforeRestore"] != true {
		t.Errorf("defaults: %v", values)
	}
	got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"agent.experiments": "variant", "agent.askBeforeRestore": false}})
	if got.Failed {
		t.Fatal(got.Text)
	}
	if v := got.Structured.(map[string]any)["values"].(map[string]any); v["agent.experiments"] != "variant" || v["agent.askBeforeRestore"] != false {
		t.Errorf("returned values: %v", v)
	}
	var prefs map[string]any
	data, _ := os.ReadFile(prefsFile)
	json.Unmarshal(data, &prefs)
	if jsonOf(prefs) != `{"agent":{"askBeforeRestore":false,"experiments":"variant","mine":1,"recordDecisions":"no"},"appearance":"dark"}` {
		t.Errorf("preferences: %s", data)
	}
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"agent.experiments": "elsewhere"}}); !got.Failed || !strings.Contains(got.Text, "sandbox, variant, ask") {
		t.Errorf("a wrong choice: %s", got.Text)
	}
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"agent.commitMilestones": "yes"}}); !got.Failed {
		t.Errorf("a string for a bool was taken: %s", got.Text)
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

// Preferences linked to another spelling of .jm.json (a case-blind volume) are
// refused too, though saves keep replacing the file while the update looks.
func TestSettingsUpdateRefusesAnotherSpellingWhileSaved(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{"name": "Neon Vow"}`})
	if _, err := os.Stat(".JM.JSON"); err != nil {
		t.Skip("case-sensitive volume")
	}
	upper, _ := filepath.Abs(".JM.JSON")
	os.MkdirAll(filepath.Dir(projects.PreferencesFile()), 0o755)
	if err := os.Symlink(upper, projects.PreferencesFile()); err != nil {
		t.Skip("no symlinks here:", err)
	}
	stop := make(chan struct{})
	saved := make(chan struct{})
	manifest, _ := filepath.Abs(".jm.json")
	go func() { // replaces .jm.json as a save does, again and again
		defer close(saved)
		for {
			select {
			case <-stop:
				return
			default:
				os.WriteFile(manifest+".new", []byte(`{"name": "Neon Vow"}`), 0o644)
				os.Rename(manifest+".new", manifest)
			}
		}
	}()
	stopSaving := sync.OnceFunc(func() { close(stop); <-saved })
	t.Cleanup(stopSaving)
	for range 200 {
		done := make(chan toolResult, 1)
		go func() {
			done <- settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"name": "X", "editor.appearance": "dark"}})
		}()
		select {
		case got := <-done:
			if !got.Failed || !strings.Contains(got.Text, "one file") {
				t.Fatalf("mixed update: %s", got.Text)
			}
		case <-time.After(5 * time.Second):
			t.Fatal("mixed update hung")
		}
	}
	stopSaving()
	if data, _ := os.ReadFile(".jm.json"); string(data) != `{"name": "Neon Vow"}` {
		t.Errorf(".jm.json changed: %s", data)
	}
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"name": "X"}}); got.Failed {
		t.Error(got.Text)
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

// The plugin's skills name only tools and agent settings jm has: a renamed
// one breaks it here, not in a person's setup.
func TestSkillsNameRealToolsAndSettings(t *testing.T) {
	scratchSettings(t)
	if skill, err := plugin.Files.ReadFile(plugin.OnboardingSkill); err != nil || !strings.HasPrefix(string(skill), "---\nname: onboarding\ndescription: ") {
		t.Fatalf("onboarding skill: %v", err)
	}
	tools := map[string]bool{} // and their arguments
	for _, tool := range newMCPServer().tools {
		tools[tool.Name] = true
		props, _ := tool.InputSchema["properties"].(map[string]any)
		for arg := range props {
			tools[arg] = true
		}
	}
	st, err := loadSettings("")
	if err != nil {
		t.Fatal(err)
	}
	// Words in backticks that are values, not tools or arguments.
	values := strings.Fields("system light dark vscode cursor zed sublime text ask scene prefab solid ground platform backup")
	skills, _ := fs.Glob(plugin.Files, "files/skills/*/SKILL.md")
	for _, path := range skills {
		skill, _ := plugin.Files.ReadFile(path)
		named := regexp.MustCompile("`([a-z]+_?[a-z_]*)`").FindAllStringSubmatch(string(skill), -1)
		for _, m := range named {
			if !tools[m[1]] && !slices.Contains(values, m[1]) {
				t.Errorf("%s names %q, no tool or argument of jm's", path, m[1])
			}
		}
		for _, m := range regexp.MustCompile(`agent\.[a-zA-Z]+`).FindAllString(string(skill), -1) {
			if _, ok := st.find(m); !ok {
				t.Errorf("%s names %q, no setting of jm's", path, m)
			}
		}
		if len(named) < 3 {
			t.Errorf("%s: found only %d names", path, len(named))
		}
	}
	if len(skills) < 8 {
		t.Errorf("skills: %v", skills)
	}
}
