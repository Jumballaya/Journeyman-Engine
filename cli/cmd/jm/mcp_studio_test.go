package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The Studio opens from the host's sidebar (games) and a chat's tabs (studio),
// fullscreen, and may run the game tools.
func TestTheStudioIsTheGlobalAndThreadEntrypoint(t *testing.T) {
	s := newMCPServer()
	for _, tool := range s.tools {
		entrypoints := jsonOf(tool.Meta["openai/ui"])
		switch tool.Name {
		case "games", "studio":
			want := map[string]string{"games": `{"entrypoints":[{"type":"global"}]}`, "studio": `{"entrypoints":[{"type":"thread"}]}`}[tool.Name]
			if entrypoints != want || tool.Meta["ui"].(map[string]any)["resourceUri"] != studio.appURI() {
				t.Errorf("%s: %v %v", tool.Name, tool.Title, tool.Meta)
			}
		case "new_game", "open_game", "project_open_editor", "project_play", "project_export", "plays_list":
			if tool.Meta["openai/widgetAccessible"] != true {
				t.Errorf("%s: the Studio can't call it", tool.Name)
			}
		}
	}
	page, _ := studio.read(studio.appURI())
	if got := jsonOf(page["_meta"].(map[string]any)["openai/ui"]); got != `{"availableDisplayModes":["fullscreen","inline"],"preferredDisplayMode":"fullscreen"}` {
		t.Errorf("display modes: %s", got)
	}
	text := page["text"].(string)
	for _, part := range []string{"const kit = ", `id: "games"`, `id: "game"`, "function jmHost("} { // tabs inlined after the kit
		if !strings.Contains(text, part) {
			t.Errorf("the page lacks %q", part)
		}
	}
}

// plays_list names its game, and lists the one asked for, whichever is open.
func TestPlaysListIsTheGameAskedFor(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"alpha", "beta"} {
		dir := filepath.Join(home, g, ".jm", "plays", g+"-play")
		os.MkdirAll(dir, 0o755)
		os.WriteFile(filepath.Join(home, g, ".jm.json"), []byte(`{"name":"`+g+`"}`), 0o644)
		os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":60,"seconds":1,"started":"2000-01-01T00:00:00"}`), 0o644)
	}
	t.Chdir(filepath.Join(home, "beta"))
	var list toolResult
	for _, tool := range newMCPServer().tools {
		if tool.Name == "plays_list" {
			list = tool.run(map[string]any{"game": filepath.Join(home, "alpha")})
		}
	}
	got := jsonOf(list.Structured)
	alpha, _ := filepath.EvalSymlinks(filepath.Join(home, "alpha"))
	if !strings.Contains(got, `"alpha-play"`) || strings.Contains(got, "beta-play") || !strings.Contains(got, jsonOf(alpha)) {
		t.Errorf("alpha's plays: %s", got)
	}
}
