package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// modernCall is a modern tools/call: caps the client's, and the answers it brings back.
func modernCall(t *testing.T, s *mcpServer, id int, tool string, args, caps map[string]any, responses map[string]any, state string) map[string]any {
	t.Helper()
	params := map[string]any{"name": tool, "arguments": args, "_meta": map[string]any{
		"io.modelcontextprotocol/protocolVersion": mcpModernVersion, "io.modelcontextprotocol/clientCapabilities": caps}}
	if responses != nil {
		params["inputResponses"] = responses
	}
	if state != "" {
		params["requestState"] = state
	}
	data, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": id, "method": "tools/call", "params": params})
	reply, _ := s.respond(data)
	out, _ := json.Marshal(reply)
	var msg map[string]any
	json.Unmarshal(out, &msg)
	return msg
}

var chatGPTCaps = map[string]any{"elicitation": map[string]any{"form": map[string]any{}},
	"extensions": map[string]any{"openai/elicitation": map[string]any{"form": map[string]any{}}}}

// Over the modern protocol a form ends the call (input_required, ChatGPT's form
// in _meta); the call again with the answer finishes it.
func TestPickAsksOverMRTR(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`, "a.scene.json": `{}`})
	s := newMCPServer()
	first := modernCall(t, s, 1, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, nil, "")["result"].(map[string]any)
	request := first["inputRequests"].(map[string]any)["form1"].(map[string]any)
	params := request["params"].(map[string]any)
	if first["resultType"] != "input_required" || request["method"] != "elicitation/create" || jsonOf(params["requestedSchema"]) != `{"properties":{},"type":"object"}` {
		t.Fatalf("first: %v", first)
	}
	if _, ok := params["_meta"].(map[string]any)["openai/elicitation"].(map[string]any)["requestedSchema"].(map[string]any)["properties"].(map[string]any)["picked"]; !ok {
		t.Errorf("ChatGPT's form isn't in _meta: %v", params)
	}
	abs, _ := filepath.Abs("a.scene.json")
	done := modernCall(t, s, 2, "pick", map[string]any{"kind": "scene"}, chatGPTCaps,
		map[string]any{"form1": accept(map[string]any{"picked": fileURI(abs)})}, first["requestState"].(string))["result"].(map[string]any)
	if done["resultType"] != "complete" || !strings.Contains(jsonOf(done["structuredContent"]), `"picked":["a.scene.json"]`) {
		t.Errorf("done: %v", done)
	}
	if bad := modernCall(t, s, 3, "new_game", map[string]any{}, chatGPTCaps, nil, first["requestState"].(string)); bad["error"] == nil {
		t.Errorf("another tool's requestState was taken: %v", bad)
	}
}

// Rounds build on each other: requestState brings back the earlier answers.
func TestNewGameWizardOverMRTR(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	t.Chdir(t.TempDir())
	defer func(v string) { version = v }(version)
	version = "v9.9.9"
	example := filepath.Join(os.Getenv("HOME"), ".jm", "examples", version, "tetris")
	os.MkdirAll(example, 0o755)
	os.WriteFile(filepath.Join(example, ".jm.json"), []byte(`{"name": "Tetris", "scenes": []}`), 0o644)
	s := newMCPServer()
	plain := map[string]any{"elicitation": map[string]any{}}
	r1 := modernCall(t, s, 1, "new_game", map[string]any{}, plain, nil, "")["result"].(map[string]any)
	if jsonOf(r1["inputRequests"].(map[string]any)["form1"].(map[string]any)["params"].(map[string]any)["requestedSchema"].(map[string]any)["properties"].(map[string]any)["from"].(map[string]any)["enum"]) != `["empty","tetris"]` {
		t.Fatalf("round 1, a plain form: %v", r1)
	}
	r2 := modernCall(t, s, 2, "new_game", map[string]any{}, plain, map[string]any{"form1": accept(map[string]any{"from": "tetris"})}, r1["requestState"].(string))["result"].(map[string]any)
	if r2["resultType"] != "input_required" || r2["inputRequests"].(map[string]any)["form2"] == nil {
		t.Fatalf("round 2: %v", r2)
	}
	r3 := modernCall(t, s, 3, "new_game", map[string]any{}, plain, map[string]any{"form2": accept(map[string]any{"name": "Blocks"})}, r2["requestState"].(string))["result"].(map[string]any)
	if r3["resultType"] != "complete" || r3["isError"] == true {
		t.Fatalf("round 3: %v", r3)
	}
	if man, err := manifest.LoadManifest(filepath.Join(home, "Blocks", ".jm.json")); err != nil || man.Name != "Blocks" {
		t.Errorf("made: %+v %v", man, err)
	}
}

// A client without forms gets none; a version jm doesn't speak is refused with the ones it does.
func TestModernRequestsWithoutFormsOrInAnotherVersion(t *testing.T) {
	scratchSettings(t)
	t.Setenv("JM_GAMES", t.TempDir())
	t.Chdir(t.TempDir())
	s := newMCPServer()
	r := modernCall(t, s, 1, "new_game", map[string]any{}, map[string]any{"extensions": map[string]any{"openai/elicitation": map[string]any{"form": map[string]any{}}}}, nil, "")["result"].(map[string]any)
	if r["isError"] != true || r["resultType"] != "complete" {
		t.Errorf("no MCP elicitation, no form: %v", r)
	}
	reply, _ := s.respond([]byte(`{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2099-01-01"}}}`))
	if reply.Error == nil || reply.Error.Code != -32022 || !strings.Contains(jsonOf(reply.Error.Data), mcpModernVersion) {
		t.Errorf("unsupported version: %+v", reply.Error)
	}
}

// Answers belong to their call: a state with the answers left out is no crash, and
// one from before another game was opened is refused.
func TestMRTRStateIsTheCallsOwn(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`, "a.scene.json": `{}`})
	s := newMCPServer()
	empty := base64.RawURLEncoding.EncodeToString([]byte(`{"call": "x"}`))
	if r := modernCall(t, s, 1, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, map[string]any{"form1": accept(nil)}, empty); r["error"] == nil {
		t.Errorf("a state for another call: %v", r)
	}
	first := modernCall(t, s, 2, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, nil, "")["result"].(map[string]any)
	t.Chdir(t.TempDir())
	if r := modernCall(t, s, 3, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, map[string]any{"form1": accept(nil)}, first["requestState"].(string)); r["error"] == nil {
		t.Errorf("answers for another game: %v", r)
	}
}

// Over HTTP a modern request's errors carry the status the transport asks for.
func TestModernHTTPErrorsHaveTheirStatus(t *testing.T) {
	for body, want := range map[string]int{
		`{"jsonrpc":"2.0","id":1,"method":"nope","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}}`: 404,
		`{"jsonrpc":"2.0","id":1,"method":"ping","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2099-01-01"}}}`:                                                 400,
		`{"jsonrpc":"2.0","id":1,"method":"nope"}`: 200,
	} {
		reply, _ := newMCPServer().respond([]byte(body))
		if got := modernStatus([]byte(body), reply); got != want {
			t.Errorf("%s: %d, want %d", body, got, want)
		}
	}
}

// modernBody is a modern request's JSON: method, params (their _meta added).
func modernBody(id int, method string, params map[string]any) string {
	params["_meta"] = map[string]any{"io.modelcontextprotocol/protocolVersion": mcpModernVersion, "io.modelcontextprotocol/clientCapabilities": map[string]any{}}
	data, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": id, "method": method, "params": params})
	return string(data)
}

// Over HTTP a modern request runs only when its headers mirror its body
// (version, method, the tool's name, base64-sentinel decoded); else 400 HeaderMismatch.
func TestModernHTTPHeadersMirrorTheBody(t *testing.T) {
	scratchSettings(t)
	games := t.TempDir()
	t.Setenv("JM_GAMES", games)
	t.Chdir(t.TempDir())
	server := newMCPServer()
	defer server.stopDriver()
	srv := httptest.NewServer(mcpHTTPHandler(server, "/mcp", nil))
	defer srv.Close()
	post := func(body string, headers map[string]string, extra http.Header) (int, map[string]any) {
		req, _ := http.NewRequest(http.MethodPost, srv.URL+"/mcp", strings.NewReader(body))
		req.Header.Set("Content-Type", "application/json")
		for k, v := range headers {
			req.Header.Set(k, v)
		}
		for k, vs := range extra {
			req.Header[http.CanonicalHeaderKey(k)] = vs
		}
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		defer resp.Body.Close()
		var msg map[string]any
		json.NewDecoder(resp.Body).Decode(&msg)
		return resp.StatusCode, msg
	}
	good := func() map[string]string {
		return map[string]string{"MCP-Protocol-Version": mcpModernVersion, "Mcp-Method": "tools/call", "Mcp-Name": "new_game"}
	}
	for i, c := range []struct {
		why     string
		headers func(map[string]string)
	}{
		{"no version", func(h map[string]string) { delete(h, "MCP-Protocol-Version") }},
		{"no method", func(h map[string]string) { delete(h, "Mcp-Method") }},
		{"no name", func(h map[string]string) { delete(h, "Mcp-Name") }},
		{"another version", func(h map[string]string) { h["MCP-Protocol-Version"] = "2025-06-18" }},
		{"another method", func(h map[string]string) { h["Mcp-Method"] = "tools/list" }},
		{"another name", func(h map[string]string) { h["Mcp-Name"] = "open_game" }},
		{"a malformed sentinel", func(h map[string]string) { h["Mcp-Name"] = "=?base64?!!!?=" }},
		{"two names", nil},
		{"another name, encoded", func(h map[string]string) {
			h["Mcp-Name"] = "=?base64?" + base64.StdEncoding.EncodeToString([]byte("open_game")) + "?="
		}},
	} {
		name := fmt.Sprintf("Refused %d", i)
		h := good()
		var extra http.Header
		if c.headers != nil {
			c.headers(h)
		} else {
			extra = http.Header{"Mcp-Name": {"new_game", "open_game"}}
		}
		status, msg := post(modernBody(i+1, "tools/call", map[string]any{"name": "new_game", "arguments": map[string]any{"name": name}}), h, extra)
		if errObj, _ := msg["error"].(map[string]any); status != 400 || errObj["code"] != float64(-32020) {
			t.Errorf("%s: %d %v", c.why, status, msg)
		}
		if exists(filepath.Join(games, name)) {
			t.Errorf("%s: the game was made anyway", c.why)
		}
	}
	h := good()
	h["Mcp-Name"] = "=?base64?" + base64.StdEncoding.EncodeToString([]byte("new_game")) + "?="
	if status, msg := post(modernBody(20, "tools/call", map[string]any{"name": "new_game", "arguments": map[string]any{"name": "Made"}}), h, nil); status != 200 || msg["error"] != nil || !exists(filepath.Join(games, "Made")) {
		t.Errorf("matching headers: %d %v", status, msg)
	}
	if status, msg := post(`{"jsonrpc":"2.0","id":21,"method":"initialize","params":{}}`, nil, nil); status != 200 || msg["result"] == nil {
		t.Errorf("legacy initialize: %d %v", status, msg)
	}
}

// requestState is the server's: changed answers, call or bytes are refused
// before anything is made; garbage is just refused.
func TestMRTRStateCantBeChanged(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	t.Chdir(t.TempDir())
	defer func(v string) { version = v }(version)
	version = "v9.9.9"
	example := filepath.Join(os.Getenv("HOME"), ".jm", "examples", version, "tetris")
	os.MkdirAll(example, 0o755)
	os.WriteFile(filepath.Join(example, ".jm.json"), []byte(`{"name": "Tetris", "scenes": []}`), 0o644)
	s := newMCPServer()
	plain := map[string]any{"elicitation": map[string]any{}}
	r1 := modernCall(t, s, 1, "new_game", map[string]any{}, plain, nil, "")["result"].(map[string]any)
	r2 := modernCall(t, s, 2, "new_game", map[string]any{}, plain, map[string]any{"form1": accept(map[string]any{"from": "tetris"})}, r1["requestState"].(string))["result"].(map[string]any)
	state := r2["requestState"].(string)
	payload, sig, _ := strings.Cut(state, ".")
	data, _ := base64.RawURLEncoding.DecodeString(payload)
	forged := base64.RawURLEncoding.EncodeToString([]byte(strings.Replace(string(data), "tetris", "empty", 1))) + "." + sig
	flipped := []byte(state)
	flipped[len(flipped)-2] ^= 1
	for why, token := range map[string]string{"another template": forged, "a changed byte": string(flipped), "garbage": "!!!", "unsigned": payload} {
		if r := modernCall(t, s, 3, "new_game", map[string]any{}, plain, map[string]any{"form2": accept(map[string]any{"name": "Forged"})}, token); r["error"] == nil {
			t.Errorf("%s: %v", why, r)
		}
	}
	if exists(filepath.Join(home, "Forged")) {
		t.Error("a game was made from a changed requestState")
	}
	if r := modernCall(t, s, 4, "new_game", map[string]any{}, plain, map[string]any{"form2": accept(map[string]any{"name": "Blocks"})}, state)["result"].(map[string]any); r["resultType"] != "complete" || r["isError"] == true {
		t.Errorf("the unchanged state: %v", r)
	}
}

// A call started outside a game runs in the game it opens, and its retry is
// bound to that game: the same retry works, after another game opens it's refused.
func TestMRTRRoundIsBoundToTheGameItRunsIn(t *testing.T) {
	scratchSettings(t)
	game := t.TempDir()
	os.WriteFile(filepath.Join(game, ".jm.json"), []byte(`{}`), 0o644)
	os.WriteFile(filepath.Join(game, "a.scene.json"), []byte(`{}`), 0o644)
	if err := projects.Remember(game); err != nil {
		t.Fatal(err)
	}
	t.Chdir(t.TempDir()) // outside any game: the call opens the remembered one
	s := newMCPServer()
	first := modernCall(t, s, 1, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, nil, "")["result"].(map[string]any)
	abs, _ := filepath.Abs("a.scene.json")
	answer := map[string]any{"form1": accept(map[string]any{"picked": fileURI(abs)})}
	if done := modernCall(t, s, 2, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, answer, first["requestState"].(string)); done["error"] != nil || done["result"].(map[string]any)["resultType"] != "complete" {
		t.Errorf("its retry: %v", done)
	}
	other := t.TempDir()
	os.WriteFile(filepath.Join(other, ".jm.json"), []byte(`{}`), 0o644)
	os.Chdir(other)
	if r := modernCall(t, s, 3, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, answer, first["requestState"].(string)); r["error"] == nil {
		t.Errorf("the retry after another game opened: %v", r)
	}
}

// Modern _meta must carry the version as a string and clientCapabilities as an object.
func TestMalformedModernMetaIsRefused(t *testing.T) {
	for _, meta := range []string{
		`{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}`,
		`{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":null}`,
		`{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":"x"}`,
		`{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":[]}`,
		`{"io.modelcontextprotocol/protocolVersion":20260728,"io.modelcontextprotocol/clientCapabilities":{}}`,
		`{"io.modelcontextprotocol/protocolVersion":null,"io.modelcontextprotocol/clientCapabilities":{}}`,
	} {
		body := `{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":` + meta + `}}`
		reply, _ := newMCPServer().respond([]byte(body))
		if reply.Error == nil || reply.Error.Code != -32602 || modernStatus([]byte(body), reply) != 400 {
			t.Errorf("%s: %+v", meta, reply.Error)
		}
	}
	reply, _ := newMCPServer().respond([]byte(modernBody(1, "ping", map[string]any{})))
	if reply.Error != nil {
		t.Errorf("clientCapabilities {}: %+v", reply.Error)
	}
}

// Modern lists and reads say how long they keep (not at all: the open game's),
// a form doesn't.
func TestModernListsSayHowToCache(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`, "a.scene.json": `{}`})
	s := newMCPServer()
	for _, body := range []string{modernBody(1, "tools/list", map[string]any{}), modernBody(2, "resources/list", map[string]any{}),
		modernBody(3, "resources/read", map[string]any{"uri": "ui://widget/journeyman-scene-app.html"})} {
		reply, _ := s.respond([]byte(body))
		if m, _ := reply.Result.(map[string]any); m == nil || m["ttlMs"] != 0 || m["cacheScope"] != "private" {
			t.Errorf("%s: %v %+v", body, reply.Result, reply.Error)
		}
	}
	if r := modernCall(t, s, 4, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, nil, "")["result"].(map[string]any); r["ttlMs"] != nil {
		t.Errorf("a form with cache hints: %v", r)
	}
}

// An accepted form without what it requires is asked again; a real answer,
// or closing it, ends the call.
func TestMRTRAsksAgainForAnIncompleteAnswer(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`, "a.scene.json": `{}`})
	s := newMCPServer()
	first := modernCall(t, s, 1, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, nil, "")["result"].(map[string]any)
	state := first["requestState"].(string)
	for i, reply := range []any{accept(nil), accept(map[string]any{}), accept(map[string]any{"other": 1.0}),
		map[string]any{"action": "accept", "content": "x"}, map[string]any{"action": "maybe"}, "nope",
		accept(map[string]any{"picked": 123.0}), accept(map[string]any{"picked": "file:///elsewhere.scene.json"})} {
		r := modernCall(t, s, 2+i, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, map[string]any{"form1": reply}, state)["result"].(map[string]any)
		if r["resultType"] != "input_required" || r["inputRequests"].(map[string]any)["form1"] == nil {
			t.Errorf("%v: %v", reply, r)
		}
	}
	abs, _ := filepath.Abs("a.scene.json")
	if r := modernCall(t, s, 10, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, map[string]any{"form1": accept(map[string]any{"picked": fileURI(abs)})}, state)["result"].(map[string]any); !strings.Contains(jsonOf(r["structuredContent"]), `"picked":["a.scene.json"]`) {
		t.Errorf("a real answer: %v", r)
	}
	if r := modernCall(t, s, 11, "pick", map[string]any{"kind": "scene"}, chatGPTCaps, map[string]any{"form1": map[string]any{"action": "decline"}}, state)["result"].(map[string]any); r["resultType"] != "complete" || strings.Contains(jsonOf(r), "a.scene.json\"]") {
		t.Errorf("closed: %v", r)
	}
}

// A plain MCP form is checked as the client was shown it: a multi-pick is one choice there.
func TestMRTRChecksThePlainFormItShowed(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`, "a.scene.json": `{}`})
	s := newMCPServer()
	plain := map[string]any{"elicitation": map[string]any{"form": map[string]any{}}}
	first := modernCall(t, s, 1, "pick", map[string]any{"kind": "scene", "many": true}, plain, nil, "")["result"].(map[string]any)
	abs, _ := filepath.Abs("a.scene.json")
	r := modernCall(t, s, 2, "pick", map[string]any{"kind": "scene", "many": true}, plain, map[string]any{"form1": accept(map[string]any{"picked": fileURI(abs)})}, first["requestState"].(string))["result"].(map[string]any)
	if r["resultType"] != "complete" || !strings.Contains(jsonOf(r["structuredContent"]), `"picked":["a.scene.json"]`) {
		t.Errorf("plain multi-pick: %v", r)
	}
}
