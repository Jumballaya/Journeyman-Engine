package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
)

// A tool as the server knows it (mcpTool), the arguments a call gives it, its result, and the pieces its input schema is made of.

type mcpTool struct {
	Name        string         `json:"name"`
	Title       string         `json:"title,omitempty"`
	Description string         `json:"description"`
	InputSchema map[string]any `json:"inputSchema"`
	// The shape of its structuredContent, when hosts rely on it (openai/settings).
	OutputSchema map[string]any `json:"outputSchema,omitempty"`
	// Its mark where hosts list it: entrypoints show jm's (newMCPServer).
	Icons []map[string]any `json:"icons,omitempty"`
	// readOnlyHint etc.: hosts skip confirming tools that only look.
	Annotations map[string]any `json:"annotations,omitempty"`
	// Host extras: a ChatGPT app's widget (openai/outputTemplate) and its kin.
	Meta map[string]any `json:"_meta,omitempty"`
	run  func(args toolArgs) toolResult
	// A tool that works on a game gives onGame instead of run: it gets the game's
	// root (the call's "game", else the open game) and the "game" argument is added.
	onGame func(root string, a toolArgs) toolResult
	orNone bool // onGame also runs with no game open (root ""): settings beyond the game's
	// A tool on the driven game (one runs at a time) gives onDriven: its game is
	// the run's, and a call that names another is refused (drivenArg is added).
	onDriven func(a toolArgs) toolResult
}

// toolResult is a tool's answer: text (and images) for the model, structured
// data for hosts that show it, and _meta only a widget sees (thumbnails as
// data URIs: kept out of the model's context).
type toolResult struct {
	Text       string
	Images     []mcpImage
	Structured any
	Meta       map[string]any
	Failed     bool
}

type mcpImage struct {
	Data     []byte
	MimeType string
}

func textResult(text string, failed bool) toolResult { return toolResult{Text: text, Failed: failed} }

func (r toolResult) reply() map[string]any {
	content := []map[string]any{{"type": "text", "text": r.Text}}
	for _, img := range r.Images {
		content = append(content, map[string]any{"type": "image", "data": base64.StdEncoding.EncodeToString(img.Data), "mimeType": img.MimeType})
	}
	out := map[string]any{"content": content, "isError": r.Failed}
	if r.Structured != nil {
		out["structuredContent"] = r.Structured
	}
	if r.Meta != nil {
		out["_meta"] = r.Meta
	}
	return out
}

func btoi(b bool) int {
	if b {
		return 1
	}
	return 0
}

// toolArgs are a tool call's arguments, read leniently: a missing or
// mistyped one is its zero value, and the tool's default applies.
type toolArgs map[string]any

// resourcePath is the file a host's file entrypoint opened, as the host says
// in the call's _meta (a type JSON arguments can't make, so never the model's).
type resourcePath string

const resourceKey = "openai/resource"

// resource is the opened file's absolute path, "" when the call isn't from one.
func (a toolArgs) resource() string {
	p, _ := a[resourceKey].(resourcePath)
	return string(p)
}

func (a toolArgs) str(key string) string {
	v, _ := a[key].(string)
	return v
}

func (a toolArgs) flag(key string) bool {
	v, _ := a[key].(bool)
	return v
}

// num is a numeric argument: fallback when absent, else clamped to [lo, hi].
func (a toolArgs) num(key string, fallback, lo, hi float64) float64 {
	v, ok := a[key].(float64)
	if !ok {
		return fallback
	}
	return min(max(v, lo), hi)
}

func (a toolArgs) list(key string) []string {
	items, _ := a[key].([]any)
	out := []string{}
	for _, item := range items {
		out = append(out, fmt.Sprint(item))
	}
	return out
}

// Schemas for tool arguments.
func object(properties map[string]any, required ...string) map[string]any {
	schema := map[string]any{"type": "object", "properties": properties}
	if len(required) > 0 {
		schema["required"] = required
	}
	return schema
}

func strArg(description string) map[string]any {
	return map[string]any{"type": "string", "description": description}
}

func boolArg(description string) map[string]any {
	return map[string]any{"type": "boolean", "description": description}
}

func intArg(description string) map[string]any {
	return map[string]any{"type": "integer", "description": description}
}

func listArg(description string) map[string]any {
	return map[string]any{"type": "array", "items": map[string]any{"type": "string"}, "description": description}
}

// readOnly marks a tool that only looks: hosts run it without asking first.
func readOnly() map[string]any { return map[string]any{"readOnlyHint": true, "openWorldHint": false} }

// writes marks a tool that changes the project or runs the game (not destructively).
func writes() map[string]any {
	return map[string]any{"readOnlyHint": false, "destructiveHint": false, "openWorldHint": false}
}

// idempotent marks a tool whose second call with the same arguments changes nothing more.
func idempotent(annotations map[string]any) map[string]any {
	annotations["idempotentHint"] = true
	return annotations
}

// Schemas for structuredContent (outputSchema).
func typed(kind string) map[string]any { return map[string]any{"type": kind} }

// jsonResult is a value as compact JSON for the model and as structured
// content for hosts (which takes an object).
func jsonResult(v any) toolResult {
	data, err := json.Marshal(v)
	if err != nil {
		return textResult(err.Error(), true)
	}
	var structured any
	_ = json.Unmarshal(data, &structured)
	if _, isObject := structured.(map[string]any); !isObject {
		structured = map[string]any{"items": structured}
	}
	return toolResult{Text: string(data), Structured: structured}
}
