package main

import (
	"encoding/json"

	"github.com/Jumballaya/Journeyman-Engine/internal/jsonfmt"
)

// jsonEditTool is a page's JSON editing done as jm fmt writes: a page edits a
// file's values, jm says what the file then is (its numbers and keys' order
// as written, the layout jm's), and the page saves that through its host.
// The layout is jm's alone: no page carries its own copy to drift.
func jsonEditTool() mcpTool {
	return mcpTool{Name: "json_edit", Title: "Lay out an edited JSON file",
		Description: "A JSON file's text with edits made, as jm fmt writes it (no edits: the file as jm fmt would write it). Nothing is saved.",
		Annotations: readOnly(), Meta: map[string]any{"ui": map[string]any{"visibility": []string{"app"}}, "openai/widgetAccessible": true},
		InputSchema: object(map[string]any{"text": strArg("the file's text as read"),
			"edits": map[string]any{"type": "array", "description": `each {"path": [keys and indexes], "value": …} or {"path": […], "remove": true}`,
				"items": map[string]any{"type": "object"}}}, "text"),
		run: func(a toolArgs) toolResult {
			var edits []jsonfmt.Edit
			if raw, err := json.Marshal(a["edits"]); err == nil && a["edits"] != nil {
				if err := json.Unmarshal(raw, &edits); err != nil {
					return textResult("edits: "+err.Error(), true)
				}
			}
			text, err := jsonfmt.Edited([]byte(a.str("text")), edits)
			if err != nil {
				return textResult("Can't make those edits: "+err.Error(), true)
			}
			return toolResult{Text: string(text), Structured: map[string]any{"text": string(text)}}
		}}
}
