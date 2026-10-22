package main

import (
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
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
	if got := jsonOf(page["_meta"].(map[string]any)["openai/ui"]); got != `{"availableDisplayModes":["fullscreen","inline","pip"],"preferredDisplayMode":"fullscreen"}` {
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

// A server started outside any game (ChatGPT starts one per chat) works on the game opened last.
func TestAServerOutsideAGameWorksOnTheLastOpened(t *testing.T) {
	scratchSettings(t)
	game := t.TempDir()
	os.MkdirAll(filepath.Join(game, "scenes"), 0o755)
	os.WriteFile(filepath.Join(game, ".jm.json"), []byte(`{"name":"Pip","scenes":["scenes/level.scene.json"]}`), 0o644)
	os.WriteFile(filepath.Join(game, "scenes", "level.scene.json"), []byte(`{"entities":[]}`), 0o644)
	t.Chdir(t.TempDir())
	empty := mcpSession(t, `{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"plays_list","arguments":{}}}`)
	if empty[1]["result"].(map[string]any)["isError"] != true {
		t.Errorf("no game anywhere, but: %v", empty[1])
	}
	if err := projects.Remember(game); err != nil {
		t.Fatal(err)
	}
	replies := mcpSession(t, `{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"mention_search","arguments":{"query":""}}}`)
	if text := jsonOf(replies[1]); !strings.Contains(text, "level") {
		t.Errorf("mentions of the last game: %s", text)
	}
}

// A play tool reads the plays of the game the call names; the driving tools
// act only on the driven game: one naming another game is refused, untouched.
func TestPlaysAndTheDriverAreTheGameNamed(t *testing.T) {
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
	s := newMCPServer()
	if got := toolOf(t, s, "play_inputs").run(toolArgs{"game": "alpha"}); !strings.Contains(got.Text, "alpha-play") {
		t.Errorf("alpha's latest play: %s", got.Text) // found, though it has no frames
	}
	if got := toolOf(t, s, "play_inputs").run(toolArgs{"game": "alpha", "play": "beta-play"}); !got.Failed {
		t.Errorf("beta's play found in alpha: %s", got.Text)
	}

	fakeDriver(s, t.TempDir(), func(string) string { return `{"ok":true,"frame":1}` })
	s.game.root = evalOrSelf(filepath.Join(home, "alpha"))
	if got := toolOf(t, s, "drive").run(toolArgs{"game": "beta", "command": "step"}); !got.Failed || !strings.Contains(got.Text, "driven is") || s.game == nil {
		t.Errorf("beta drove alpha's run: %s", got.Text)
	}
	if got := toolOf(t, s, "drive").run(toolArgs{"game": "alpha", "command": "step"}); got.Failed {
		t.Errorf("alpha's run: %s", got.Text)
	}
	if got := toolOf(t, s, "drive_stop").run(toolArgs{}); s.game != nil || got.Failed {
		t.Errorf("drive_stop: %s", got.Text)
	}
}
