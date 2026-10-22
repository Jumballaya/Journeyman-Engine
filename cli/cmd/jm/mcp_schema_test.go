package main

import (
	"os"
	"strings"
	"testing"
)

// A call's arguments are checked against its tool's schema as it arrives:
// one given of the wrong type is refused (saying what it must be), one left
// out is the tool's default, and a bound is the tool's to clamp.
func TestAToolsArgumentsAreItsSchemas(t *testing.T) {
	t.Chdir(t.TempDir())
	os.WriteFile(".jm.json", []byte(`{}`), 0o644)
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"files_search","arguments":{"text":123}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"vcs_restore","arguments":{"files":["a.json",7]}}}`,
		`{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"forget","arguments":{"id":"f1","scope":"everyone"}}}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"memory_note","arguments":{}}}`,
		`{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"world_context","arguments":{"note":"Design","steps":1.5}}}`,
		`{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"files_search","arguments":{"text":"x","game":null}}}`,
		`{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"asset_add","arguments":{"file":{"download_url":"https://x/h.png","file_name":"h.png"}}}}`,
		`{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"asset_add","arguments":{"file":{"download_url":"https://x/h.png","file_id":"f","file_name":7}}}}`)
	for id, want := range map[float64]string{1: "argument text must be text", 2: "argument files must be a list whose item 2 is text",
		3: "argument scope must be one of", 4: "argument text is needed", 5: "argument steps must be a whole number",
		7: "argument file must be an object with file_id", 8: "argument file must be an object whose file_name is text"} {
		result, _ := replies[id]["result"].(map[string]any)
		if text := jsonOf(replies[id]); result["isError"] != true || !strings.Contains(text, want) {
			t.Errorf("call %v: want %q: %s", id, want, text)
		}
	}
	if result, _ := replies[6]["result"].(map[string]any); result["isError"] == true {
		t.Errorf("a null argument is one left out: %s", jsonOf(replies[6]))
	}
}

// A tool reads only what its schema declares: under test, reading another fails.
func TestAToolReadingAnUndeclaredArgumentFails(t *testing.T) {
	a := toolArgs{schemaKey: object(map[string]any{"text": strArg("")}), "text": "x", "server": true}
	if a.str("text") != "x" {
		t.Fatal("a declared argument")
	}
	defer func() {
		if recover() == nil {
			t.Error("reading undeclared server didn't fail")
		}
	}()
	a.flag("server")
}

// A form's field and a setting are checked as a tool's argument is: an
// integer field takes a whole number only.
func TestAFormsIntegerIsWhole(t *testing.T) {
	schema := object(map[string]any{"count": intArg("")}, "count")
	if fills(map[string]any{"count": 1.5}, schema) || !fills(map[string]any{"count": 2.0}, schema) {
		t.Error("a form took 1.5 for an integer, or refused 2")
	}
}

// A multi-select's choices are its list's: a file no longer offered isn't taken.
func TestAMultiSelectTakesOnlyItsChoices(t *testing.T) {
	field := map[string]any{"type": "array", "items": map[string]any{"type": "string", "format": "uri"},
		"x-openai-input": map[string]any{"options": []map[string]any{{"uri": "jm://a", "name": "a"}}}}
	if schemaError(field, []any{"jm://a"}) != nil || schemaError(field, []any{"jm://a", "jm://gone"}) == nil {
		t.Error("a multi-select took what it doesn't offer, or refused what it does")
	}
}
