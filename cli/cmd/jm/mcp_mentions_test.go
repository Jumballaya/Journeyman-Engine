package main

import (
	"image/color"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// A game with a scene (two bricks and a player), a prefab, a script and two
// plays, the latest with a marker the person noted.
func mentionGame(t *testing.T) {
	t.Helper()
	t.Chdir(t.TempDir())
	for path, text := range map[string]string{
		".jm.json":                        `{}`,
		"scenes/level1.scene.json":        `{"entities":[{"name":"Brick"},{"name":"Player","components":{"TransformComponent":{"x":4}}},{"name":"Brick"}]}`,
		"assets/prefabs/coin.prefab.json": `{}`,
		"assets/scripts/player.ts":        "",
		"assets/scripts/env.d.ts":         "",
		"build/scenes/level1.scene.json":  `{"entities":[{"name":"Stale"}]}`,
	} {
		os.MkdirAll(filepath.Dir(path), 0o755)
		os.WriteFile(path, []byte(text), 0o644)
	}
	for id, meta := range map[string]string{
		"2000-01-01_000000": `{"format":1,"frames":60,"seconds":1,"started":"2000-01-01T00:00:00"}`,
		"2000-01-02_000000": `{"format":1,"frames":300,"seconds":5,"started":"2000-01-02T00:00:00",
			"markers":[{"n":1,"frame":180,"time":3,"scene":"scenes/level1.scene.json","note":"too floaty","image":"m1.png"}]}`,
	} {
		dir := filepath.Join(plays.Root("."), id)
		os.MkdirAll(dir, 0o755)
		os.WriteFile(filepath.Join(dir, "session.json"), []byte(meta), 0o644)
	}
	writePNG(t, filepath.Join(plays.Root("."), "2000-01-02_000000", "m1.png"), 32, 18, color.White)
}

func mentionNames(found []mention) string {
	var names []string
	for _, m := range found {
		names = append(names, m.kind+":"+m.name)
	}
	return strings.Join(names, ",")
}

func TestMentionsOfferTheLatestPlayFirst(t *testing.T) {
	mentionGame(t)
	got := mentionNames(searchMentions(mentionables(), "", mentionLimit))
	want := "play:play 2000-01-02_000000,marker:marker 1: too floaty,scene:level1,prefab:coin,script:player," +
		"entity:Brick,entity:Player,play:play 2000-01-01_000000"
	if got != want {
		t.Errorf("got  %s\nwant %s", got, want)
	}
}

func TestMentionsMatchNamesFirst(t *testing.T) {
	mentionGame(t)
	for query, want := range map[string]string{
		"pla":    "play:play 2000-01-02_000000,script:player,entity:Player,play:play 2000-01-01_000000,marker:marker 1: too floaty",
		"FLOATY": "marker:marker 1: too floaty",
		"zzz":    "",
	} {
		if got := mentionNames(searchMentions(mentionables(), query, mentionLimit)); got != want {
			t.Errorf("%q: got %s, want %s", query, got, want)
		}
	}
	if got := len(searchMentions(mentionables(), "", 3)); got != 3 {
		t.Errorf("limit: %d", got)
	}
}

// What a mention links to reads back: a play's overview, a marker with what
// the person saw, every entity of a name in its scene.
func TestMentionedThingsRead(t *testing.T) {
	mentionGame(t)
	links := map[string]string{}
	for _, m := range mentionables() {
		links[m.kind+":"+m.name] = m.uri
	}
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"mention_search","arguments":{"query":"brick"}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"resources/read","params":{"uri":"`+links["play:play 2000-01-02_000000"]+`"}}`,
		`{"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"`+links["marker:marker 1: too floaty"]+`"}}`,
		`{"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"`+links["entity:Brick"]+`"}}`,
		`{"jsonrpc":"2.0","id":5,"method":"resources/read","params":{"uri":"jm://entity?scene=..%2Fx.scene.json&name=Brick"}}`,
		`{"jsonrpc":"2.0","id":6,"method":"resources/read","params":{"uri":"jm://play/2000-01-02/m1"}}`)
	items := replies[1]["result"].(map[string]any)["structuredContent"].(map[string]any)["items"].([]any)
	link := items[0].(map[string]any)
	if len(items) != 1 || link["type"] != "resource_link" || link["name"] != "Brick" || link["description"] != "entity · 2 in level1" ||
		link["uri"] != entityURI("scenes/level1.scene.json", "Brick") {
		t.Errorf("search: %v", items)
	}
	contents := func(id float64) []any { return replies[id]["result"].(map[string]any)["contents"].([]any) }
	if text := contents(2)[0].(map[string]any)["text"].(string); !strings.Contains(text, `marker 1 at 0:03.0 (frame 180, scenes/level1.scene.json): "too floaty"`) {
		t.Errorf("play: %s", text)
	}
	marker := contents(3)
	if len(marker) != 2 || marker[1].(map[string]any)["mimeType"] != "image/jpeg" || !strings.Contains(marker[0].(map[string]any)["text"].(string), `"too floaty"`) {
		t.Errorf("marker: %v", marker)
	}
	if text := contents(4)[0].(map[string]any)["text"].(string); strings.Count(text, `"where"`) != 2 || !strings.Contains(text, `"where": "entities[2]"`) {
		t.Errorf("entity: %s", text)
	}
	for _, id := range []float64{5, 6} {
		if replies[id]["error"] == nil {
			t.Errorf("%v: read %v", id, replies[id]["result"])
		}
	}
}

func TestMentionSearchIsAdvertised(t *testing.T) {
	caps := mcpCapabilities()
	for _, where := range []string{"extensions", "experimental"} {
		if got := jsonOf(caps[where].(map[string]any)["openai/mentions"]); got != `{"searchTool":"mention_search"}` {
			t.Errorf("%s: %s", where, got)
		}
	}
	for _, tool := range newMCPServer().tools {
		if tool.Name == mentionTool && (tool.Annotations["readOnlyHint"] != true || jsonOf(tool.Meta["ui"]) != `{"visibility":["app"]}`) {
			t.Errorf("mention_search: %v %v", tool.Annotations, tool.Meta)
		}
	}
}

// Children are entities too; links out of the project aren't the project's.
func TestMentionsReachChildrenButNotOutOfTheProject(t *testing.T) {
	mentionGame(t)
	outside := t.TempDir()
	os.WriteFile(filepath.Join(outside, "x.scene.json"), []byte(`{"entities":[{"name":"Secret"}]}`), 0o644)
	os.Symlink(filepath.Join(outside, "x.scene.json"), "scenes/linked.scene.json")
	writePNG(t, filepath.Join(outside, "o.png"), 8, 8, color.Black)
	marker := filepath.Join(plays.Root("."), "2000-01-02_000000", "m1.png")
	os.Remove(marker)
	os.Symlink(filepath.Join(outside, "o.png"), marker)
	os.WriteFile("scenes/boss #2.scene.json", []byte(`{"entities":[{"name":"Kage","children":[{"name":"KatanaLight"}]}]}`), 0o644)

	found := map[string]mention{}
	for _, m := range mentionables() {
		found[m.kind+":"+m.name] = m
	}
	if _, ok := found["entity:Secret"]; ok {
		t.Error("a scene linked from outside the project was read")
	}
	light, ok := found["entity:KatanaLight"]
	if !ok {
		t.Fatalf("a child entity isn't offered: %v", mentionNames(mentionables()))
	}
	contents, _, err := readMentioned(light.uri)
	if err != nil || !strings.Contains(contents[0]["text"].(string), `"where": "entities[0].children[0]"`) {
		t.Errorf("child: %v %v", contents, err)
	}
	if text, _, err := readResource(found["scene:boss #2"].uri); err != nil || !strings.Contains(text, "Kage") {
		t.Errorf("a scene named with # and a space reads back from its link %s: %v", found["scene:boss #2"].uri, err)
	}
	contents, _, err = readMentioned(found["marker:marker 1: too floaty"].uri)
	if err != nil || len(contents) != 1 {
		t.Errorf("a marker image linked from outside the play was read: %d contents, %v", len(contents), err)
	}
}

func TestMentionsLeaveOutLinksIntoHiddenFolders(t *testing.T) {
	mentionGame(t)
	os.WriteFile("raw.json", []byte(`{"entities":[{"name":"Raw"}]}`), 0o644)
	os.WriteFile("raw.bin", []byte{0}, 0o644)
	if os.Symlink("../build/scenes/level1.scene.json", "scenes/stale.scene.json") != nil ||
		os.Symlink("level1.scene.json", "scenes/again.scene.json") != nil ||
		os.Symlink("../raw.json", "scenes/raw.scene.json") != nil ||
		os.Symlink("../../raw.bin", "assets/scripts/raw.ts") != nil {
		t.Skip("no symlinks here")
	}
	found := map[string]mention{}
	for _, m := range mentionables() {
		found[m.kind+":"+m.name] = m
	}
	if _, ok := found["entity:Stale"]; ok {
		t.Error("a scene linked from build/ was read")
	}
	if _, ok := found["scene:stale"]; ok {
		t.Error("a link into build/ is offered")
	}
	for _, name := range []string{"scene:raw", "entity:Raw", "script:raw"} {
		if _, ok := found[name]; ok {
			t.Errorf("%s is offered, but its link leads to another kind of file: %s", name, mentionNames(mentionables()))
		}
	}
	again, ok := found["scene:again"]
	if !ok {
		t.Fatalf("a link to a scene of the project isn't offered: %v", mentionNames(mentionables()))
	}
	if text, _, err := readResource(again.uri); err != nil || !strings.Contains(text, "Player") {
		t.Errorf("a link to a scene of the project doesn't read back: %v", err)
	}
}

// design/ linked to a folder in the game: its notes are mentions and jm://memory's, as memory reads them.
func TestNotesThroughADesignLinkAreMentionedAndRead(t *testing.T) {
	mentionGame(t)
	cwd, _ := os.Getwd()
	real := filepath.Join(cwd, "docs", "design")
	os.MkdirAll(real, 0o755)
	os.WriteFile(filepath.Join(real, "Pacing.md"), []byte("---\ntitle: Pacing\n---\n\n# Pacing\n"), 0o644)
	if os.Symlink(real, "design") != nil {
		t.Skip("no symlinks here")
	}
	found := map[string]mention{}
	for _, m := range mentionables() {
		found[m.kind+":"+m.name] = m
	}
	if _, ok := found["note:Pacing"]; !ok {
		t.Errorf("no note mention: %s", mentionNames(mentionables()))
	}
	if text, _, err := readResource("jm://memory"); err != nil || !strings.Contains(text, "Pacing") {
		t.Errorf("jm://memory: %v %s", err, text)
	}
}
