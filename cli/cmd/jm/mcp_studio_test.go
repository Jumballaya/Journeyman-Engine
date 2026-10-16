package main

import (
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
	for _, part := range []string{"const kit = ", `id: "games"`, `id: "game"`, "function jmHost("} {
		if !strings.Contains(text, part) {
			t.Errorf("the page lacks %q", part)
		}
	}
	if strings.Contains(text, "__JM_STUDIO__") {
		t.Error("the tabs weren't inlined")
	}
}
