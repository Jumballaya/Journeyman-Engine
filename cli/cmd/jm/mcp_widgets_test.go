package main

import (
	"encoding/json"
	"strings"
	"testing"
)

// Hosts find the extensions where their protocol looks, and jm's icon.
func TestMCPAdvertisesExtensionsAndIcons(t *testing.T) {
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"server/discover","params":{}}`)
	initialize, discover := replies[1]["result"].(map[string]any), replies[2]["result"].(map[string]any)
	for name, caps := range map[string]any{"initialize": initialize["capabilities"], "server/discover": discover["capabilities"]} {
		caps := caps.(map[string]any)
		for _, where := range []string{"extensions", "experimental"} {
			if _, ok := caps[where].(map[string]any)["io.modelcontextprotocol/ui"]; !ok {
				t.Errorf("%s: no MCP Apps in capabilities.%s: %v", name, where, caps)
			}
		}
	}
	if v := discover["supportedVersions"].([]any); len(v) != 2 || v[0] != mcpModernVersion || v[1] != mcpProtocolVersion {
		t.Errorf("discover offers the modern version and the one initialize speaks: %v", v)
	}
	for _, info := range []any{initialize["serverInfo"], discover["_meta"].(map[string]any)["io.modelcontextprotocol/serverInfo"]} {
		icons := info.(map[string]any)["icons"].([]any)
		if len(icons) != 2 || !strings.HasPrefix(icons[0].(map[string]any)["src"].(string), "data:image/svg+xml;base64,") {
			t.Errorf("icons: %v", icons)
		}
	}
}

// A widget is served as an MCP App and as ChatGPT's skybridge page, bridge inlined.
func TestMCPWidgetsAreServedWithTheirBridge(t *testing.T) {
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"resources/list"}`,
		`{"jsonrpc":"2.0","id":2,"method":"resources/read","params":{"uri":"ui://widget/journeyman-plays-app.html"}}`,
		`{"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"ui://widget/journeyman-plays.html"}}`)
	listed := map[string]string{}
	for _, r := range replies[1]["result"].(map[string]any)["resources"].([]any) {
		r := r.(map[string]any)
		if strings.HasPrefix(r["uri"].(string), "ui://") {
			listed[r["uri"].(string)] = r["mimeType"].(string)
		}
	}
	want := map[string]string{}
	for _, w := range widgets {
		want[w.appURI()], want[w.legacyURI()] = "text/html;profile=mcp-app", "text/html+skybridge"
	}
	if a, b := jsonOf(listed), jsonOf(want); a != b {
		t.Fatalf("widgets listed: %s, want %s", a, b)
	}
	for id, uri := range map[float64]string{2: "ui://widget/journeyman-plays-app.html", 3: "ui://widget/journeyman-plays.html"} {
		content := replies[id]["result"].(map[string]any)["contents"].([]any)[0].(map[string]any)
		page := content["text"].(string)
		if !strings.Contains(page, "function jmHost(") || strings.Contains(page, "__JM_BRIDGE__") || strings.Contains(page, `"__JM_VERSION__"`) {
			t.Errorf("%s: served without its bridge or version", uri)
		}
		if content["mimeType"] != want[uri] {
			t.Errorf("%s: %v", uri, content["mimeType"])
		}
	}
	app := replies[2]["result"].(map[string]any)["contents"].([]any)[0].(map[string]any)["_meta"].(map[string]any)
	if got := jsonOf(app["openai/ui"]); got != `{"availableDisplayModes":["inline","fullscreen"],"preferredDisplayMode":"inline"}` {
		t.Errorf("display modes: %s", got)
	}
}

func TestWidgetToolMetaNamesItsEntrypoints(t *testing.T) {
	meta := timeline.toolMeta("", "", entrypoint("thread"), fileEntrypoint(".scene.json"))
	if got := jsonOf(meta["openai/ui"]); got != `{"entrypoints":[{"type":"thread"},{"extensions":[".scene.json"],"type":"file"}]}` {
		t.Errorf("entrypoints: %s", got)
	}
	if meta["ui"].(map[string]any)["resourceUri"] != timeline.appURI() || meta["openai/outputTemplate"] != timeline.legacyURI() {
		t.Errorf("meta: %v", meta)
	}
	if _, ok := timeline.toolMeta("", "")["openai/ui"]; ok {
		t.Error("a tool without entrypoints names none")
	}
}

func jsonOf(v any) string {
	data, _ := json.Marshal(v)
	return string(data)
}

// Tools the host opens from its sidebar or tabs carry jm's mark, one color for the host to tint.
func TestEntrypointsCarryJMsMark(t *testing.T) {
	for _, tool := range newMCPServer().tools {
		_, entry := tool.Meta["openai/ui"]
		if entry != (len(tool.Icons) == 2) || entry != (tool.Meta["openai/iconStyle"] == "monochrome") {
			t.Errorf("%s: entrypoint %v, icons %d, style %v", tool.Name, entry, len(tool.Icons), tool.Meta["openai/iconStyle"])
		}
	}
}
