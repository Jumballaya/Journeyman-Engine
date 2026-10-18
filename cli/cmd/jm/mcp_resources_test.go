package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"
)

// pipeSession serves s over stdio in the current folder, checking subscriptions
// every 10ms: send writes a message, next reads one (nil: nothing for a while).
func pipeSession(t *testing.T, s *mcpServer) (send func(string), next func(wait time.Duration) map[string]any) {
	t.Helper()
	s.pollEvery = 10 * time.Millisecond
	inR, inW := io.Pipe()
	outR, outW := io.Pipe()
	go func() { s.serve(inR, outW); outW.Close() }()
	t.Cleanup(func() { inW.Close(); io.Copy(io.Discard, outR) })
	messages := make(chan map[string]any, 100)
	go func() {
		for lines := bufio.NewScanner(outR); lines.Scan(); {
			var msg map[string]any
			json.Unmarshal(lines.Bytes(), &msg)
			messages <- msg
		}
	}()
	send = func(line string) { io.WriteString(inW, line+"\n") }
	next = func(wait time.Duration) map[string]any {
		select {
		case msg := <-messages:
			return msg
		case <-time.After(wait):
			return nil
		}
	}
	return send, next
}

// request sends a request and gives its answer, and the notifications before it ("method uri").
func request(t *testing.T, send func(string), next func(time.Duration) map[string]any, id int, method, params string) (map[string]any, []string) {
	t.Helper()
	send(fmt.Sprintf(`{"jsonrpc":"2.0","id":%d,"method":%q,"params":%s}`, id, method, params))
	var notes []string
	for {
		msg := next(10 * time.Second)
		if msg == nil {
			t.Fatalf("%s: no answer", method)
		}
		if msg["id"] == float64(id) {
			return msg, notes
		}
		notes = append(notes, note(msg))
	}
}

func note(msg map[string]any) string {
	params, _ := msg["params"].(map[string]any)
	uri, _ := params["uri"].(string)
	return strings.TrimSpace(fmt.Sprint(msg["method"], " ", uri))
}

// waitFor reads messages until one is method, failing on any other first.
func waitFor(t *testing.T, next func(time.Duration) map[string]any, want string) {
	t.Helper()
	for {
		msg := next(5 * time.Second)
		if msg == nil {
			t.Fatalf("no %s", want)
		}
		if got := note(msg); got == want {
			return
		} else if got != "notifications/message" {
			t.Fatalf("got %s, want %s", got, want)
		}
	}
}

func TestResourceTemplatesAreListedAndAGamesFileReads(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	game, err := newGame(context.Background(), home, newGameChoice{name: "Neon Vow"})
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(game, "notes.txt"), []byte("hello"), 0o644)
	t.Chdir(t.TempDir())
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"resources/templates/list"}`,
		`{"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"jm://game/Neon%20Vow/file/notes.txt"}}`,
		`{"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"jm://game/Neon%20Vow/file/..%2F..%2Fsecret"}}`,
		`{"jsonrpc":"2.0","id":5,"method":"resources/read","params":{"uri":"jm://game/Neon%20Vow/file/.jm%2Fx"}}`)
	caps := replies[1]["result"].(map[string]any)["capabilities"].(map[string]any)
	if jsonOf(caps["resources"]) != `{"listChanged":true,"subscribe":true}` || caps["completions"] == nil {
		t.Errorf("capabilities: %v", caps)
	}
	var uris []string
	for _, tmpl := range replies[2]["result"].(map[string]any)["resourceTemplates"].([]any) {
		uris = append(uris, tmpl.(map[string]any)["uriTemplate"].(string))
	}
	if got := strings.Join(uris, " "); got != "jm://game/{game}/file/{path} jm://play/{play} jm://play/{play}/{marker} jm://entity{?scene,name} jm://docs/{topic}" {
		t.Errorf("templates: %s", got)
	}
	if got := jsonOf(replies[3]["result"]); got != `{"contents":[{"mimeType":"text/plain","text":"hello","uri":"jm://game/Neon%20Vow/file/notes.txt"}]}` {
		t.Errorf("read: %s", got)
	}
	for _, id := range []float64{4, 5} {
		if replies[id]["error"] == nil {
			t.Errorf("%v read outside the game's files: %v", id, replies[id]["result"])
		}
	}
	if caps := (&mcpServer{}).capabilities(); jsonOf(caps["resources"]) != `{}` {
		t.Errorf("HTTP can't push, so it mustn't offer to: %v", caps["resources"])
	}
}

// A subscribed file's change is sent until it's unsubscribed; a new file changes the list.
func TestSubscriptionsSayWhatChanged(t *testing.T) {
	scratchSettings(t)
	mentionGame(t)
	scene, _ := filepath.Abs("scenes/level1.scene.json")
	uri := fileURI(scene)
	send, next := pipeSession(t, newMCPServer())
	request(t, send, next, 1, "resources/list", `{}`)
	if reply, _ := request(t, send, next, 2, "resources/subscribe", `{"uri":"`+uri+`"}`); reply["error"] != nil {
		t.Fatal(reply["error"])
	}
	for i, bad := range []string{`{"uri":"file:///etc/hosts"}`, `{}`, `{"uri":"jm://nope"}`} {
		if reply, _ := request(t, send, next, 10+i, "resources/subscribe", bad); reply["error"] == nil {
			t.Errorf("%s: only the game's resources can be watched", bad)
		}
	}
	os.WriteFile(scene, []byte(`{"entities":[]}`), 0o644)
	waitFor(t, next, "notifications/resources/updated "+uri)

	request(t, send, next, 4, "resources/unsubscribe", `{"uri":"`+uri+`"}`)
	os.WriteFile(scene, []byte(`{"entities":[{}]}`), 0o644)
	os.WriteFile("scenes/level2.scene.json", []byte(`{}`), 0o644)
	waitFor(t, next, "notifications/resources/list_changed") // and no update before it
	if msg := next(100 * time.Millisecond); msg != nil {
		t.Errorf("after: %v", msg)
	}
}

func TestCompletionsSuggestWhatStartsSo(t *testing.T) {
	scratchSettings(t)
	t.Setenv("JM_GAMES", t.TempDir())
	mentionGame(t)
	for i := range 150 {
		os.WriteFile(fmt.Sprintf("scenes/z%03d.scene.json", i), []byte(`{}`), 0o644)
	}
	s := newMCPServer()
	complete := func(ref, arg, value, context string) map[string]any {
		reply, ok := s.respond(fmt.Appendf(nil, `{"jsonrpc":"2.0","id":1,"method":"completion/complete","params":{"ref":%s,"argument":{"name":%q,"value":%q},"context":{"arguments":%s}}}`, ref, arg, value, context))
		if !ok || reply.Error != nil {
			t.Fatalf("%s %s: %v", ref, arg, reply.Error)
		}
		return reply.Result.(map[string]any)["completion"].(map[string]any)
	}
	entity := `{"type":"ref/resource","uri":"jm://entity{?scene,name}"}`
	marker := `{"type":"ref/resource","uri":"jm://play/{play}/{marker}"}`
	for _, c := range []struct{ ref, arg, value, context, want string }{
		{entity, "scene", "scenes/L", `{}`, `["scenes/level1.scene.json"]`},
		{entity, "name", "b", `{"scene":"scenes/level1.scene.json"}`, `["Brick"]`},
		{entity, "name", "", `{"scene":"../x.scene.json"}`, `[]`},
		{`{"type":"ref/resource","uri":"jm://play/{play}"}`, "play", "2000-01-02", `{}`, `["2000-01-02_000000"]`},
		{marker, "marker", "", `{"play":"2000-01-02_000000"}`, `["m1"]`},
		{marker, "marker", "", `{"play":"2000-01-01_000000"}`, `[]`},
		{`{"type":"ref/resource","uri":"jm://docs/{topic}"}`, "topic", "agen", `{}`, `["agents"]`},
		{`{"type":"ref/prompt","name":"new_game_interview"}`, "x", "", `{}`, `[]`},
	} {
		if got := jsonOf(complete(c.ref, c.arg, c.value, c.context)["values"]); got != c.want {
			t.Errorf("%s %s %q: %s, want %s", c.ref, c.arg, c.value, got, c.want)
		}
	}
	if got := complete(entity, "scene", "scenes/z", `{}`); len(got["values"].([]string)) != 100 || got["total"] != 150 || got["hasMore"] != true {
		t.Errorf("past 100: %v %v", got["total"], got["hasMore"])
	}
	open, _ := os.Getwd() // the open game, though it isn't in the games list (opened from a root, say)
	if got := complete(`{"type":"ref/resource","uri":"jm://game/{game}/file/{path}"}`, "game", "", `{}`)["values"].([]string); !slices.Contains(got, open) {
		t.Errorf("games: %v, want the open %s among them", got, open)
	}
	if reply, _ := s.respond([]byte(`{"jsonrpc":"2.0","id":1,"method":"completion/complete","params":{"ref":{"type":"ref/resource","uri":"jm://nope/{x}"},"argument":{"name":"x","value":""}}}`)); reply.Error == nil {
		t.Error("an unknown template must fail")
	}
}

// Started in no game, the server opens the client's root that's a game.
func TestRootsOpenTheGame(t *testing.T) {
	scratchSettings(t)
	t.Setenv("JM_GAMES", t.TempDir())
	game := t.TempDir()
	os.WriteFile(filepath.Join(game, ".jm.json"), []byte(`{"name":"Rooted"}`), 0o644)
	t.Chdir(t.TempDir())
	send, next := pipeSession(t, newMCPServer())
	request(t, send, next, 1, "initialize", `{"protocolVersion":"2025-06-18","capabilities":{"roots":{"listChanged":true}}}`)
	send(`{"jsonrpc":"2.0","method":"notifications/initialized"}`)
	ask := next(5 * time.Second)
	if ask == nil || ask["method"] != "roots/list" {
		t.Fatalf("no roots/list: %v", ask)
	}
	id, _ := json.Marshal(ask["id"])
	send(fmt.Sprintf(`{"jsonrpc":"2.0","id":%s,"result":{"roots":[{"uri":"%s"},{"uri":"%s"}]}}`, id, fileURI(t.TempDir()), fileURI(game)))
	reply, _ := request(t, send, next, 2, "tools/call", `{"name":"games","arguments":{}}`)
	text := jsonOf(reply["result"])
	if want, _ := filepath.EvalSymlinks(game); !strings.Contains(text, "Open: Rooted ("+want+")") {
		t.Errorf("games: %s", text)
	}
}
