package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// mcpSession sends requests (one JSON-RPC message per line) and returns the
// replies by id.
func mcpSession(t *testing.T, requests ...string) map[float64]map[string]any {
	t.Helper()
	var out bytes.Buffer
	if err := serveMCP(strings.NewReader(strings.Join(requests, "\n")), &out); err != nil {
		t.Fatal(err)
	}
	replies := map[float64]map[string]any{}
	for _, line := range strings.Split(strings.TrimSpace(out.String()), "\n") {
		var msg map[string]any
		if err := json.Unmarshal([]byte(line), &msg); err != nil {
			t.Fatalf("not JSON: %s", line)
		}
		id, _ := msg["id"].(float64)
		replies[id] = msg
	}
	return replies
}

func TestMCPInitializesAndListsTools(t *testing.T) {
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"0"}}}`,
		`{"jsonrpc":"2.0","method":"notifications/initialized"}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/list"}`,
		`{"jsonrpc":"2.0","id":3,"method":"nope"}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"drive","arguments":{"command":"step"}}}`)
	if len(replies) != 4 {
		t.Fatalf("a notification gets no reply: got %d replies", len(replies))
	}
	info := replies[1]["result"].(map[string]any)
	if info["protocolVersion"] != mcpProtocolVersion || info["serverInfo"].(map[string]any)["name"] != "journeyman" {
		t.Fatalf("initialize: %v", info)
	}
	names := []string{}
	for _, tool := range replies[2]["result"].(map[string]any)["tools"].([]any) {
		names = append(names, tool.(map[string]any)["name"].(string))
	}
	if strings.Join(names, ",") != "build,test,golden,schema,generate,drive_start,drive,drive_stop" {
		t.Fatalf("tools: %v", names)
	}
	if replies[3]["error"].(map[string]any)["code"].(float64) != -32601 {
		t.Fatalf("an unknown method is an error: %v", replies[3])
	}
	result := replies[4]["result"].(map[string]any)
	if result["isError"] != true || !strings.Contains(result["content"].([]any)[0].(map[string]any)["text"].(string), "drive_start") {
		t.Fatalf("driving with no game running should say to start one: %v", result)
	}
}

// Resources are the project's files (not build/ or node_modules), read only inside the project.
func TestMCPResourcesAreTheProjectsFiles(t *testing.T) {
	dir := t.TempDir()
	for path, text := range map[string]string{
		".jm.json": "{}", "scenes/main.scene.json": "{}", "assets/scripts/hero.ts": "// hero",
		"build/scenes/main.scene.json": "{}", "assets/scripts/node_modules/x/index.ts": "", "assets/hero.png": "",
	} {
		full := filepath.Join(dir, path)
		if err := os.MkdirAll(filepath.Dir(full), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(text), 0644); err != nil {
			t.Fatal(err)
		}
	}
	t.Chdir(dir)
	names := []string{}
	for _, r := range projectResources() {
		names = append(names, r["name"].(string))
	}
	if strings.Join(names, ",") != "schema,.jm.json,assets/scripts/hero.ts,scenes/main.scene.json" {
		t.Fatalf("resources: %v", names)
	}
	abs, _ := filepath.Abs("assets/scripts/hero.ts")
	if text, mime, err := readResource("file://" + filepath.ToSlash(abs)); err != nil || text != "// hero" || mime != "text/x-typescript" {
		t.Fatalf("read: %q %q %v", text, mime, err)
	}
	if _, _, err := readResource("file:///etc/hosts"); err == nil {
		t.Fatal("files outside the project can't be read")
	}
}
