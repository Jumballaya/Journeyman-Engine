package main

import (
	"encoding/base64"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
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
		`{"jsonrpc":"2.0","id":1,"method":"nope","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}}`: 404,
		`{"jsonrpc":"2.0","id":1,"method":"ping","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2099-01-01"}}}`: 400,
		`{"jsonrpc":"2.0","id":1,"method":"nope"}`: 200,
	} {
		reply, _ := newMCPServer().respond([]byte(body))
		if got := modernStatus([]byte(body), reply); got != want {
			t.Errorf("%s: %d, want %d", body, got, want)
		}
	}
}
