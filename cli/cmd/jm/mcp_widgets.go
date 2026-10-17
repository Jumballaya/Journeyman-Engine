package main

import (
	_ "embed"
	"encoding/base64"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/plugin"
)

// Widgets: the HTML pages jm's tools show in hosts that render them. Each is
// served twice: as an MCP App (ui://…-app.html, the standard) and as ChatGPT's
// older skybridge widget. Pages get widget/bridge.js inlined at the
// __JM_BRIDGE__ comment and this jm's version for __JM_VERSION__.

//go:embed widget/bridge.js
var widgetBridge string

type widget struct {
	name, title, description string
	html                     string
	// Display modes the page works in, first preferred (ChatGPT opens
	// entrypoints fullscreen whatever this says).
	modes []string
}

func (w widget) appURI() string    { return "ui://widget/" + w.name + "-app.html" }
func (w widget) legacyURI() string { return "ui://widget/" + w.name + ".html" }

// toolMeta marks a tool whose result w renders, with what the host says
// while it runs; entrypoints (entrypoint(...)) open it from the host's UI too.
func (w widget) toolMeta(invoking, invoked string, entrypoints ...map[string]any) map[string]any {
	meta := callableMeta(invoking, invoked)
	meta["openai/outputTemplate"] = w.legacyURI()
	meta["ui"] = map[string]any{"resourceUri": w.appURI()}
	if len(entrypoints) > 0 {
		meta["openai/ui"] = map[string]any{"entrypoints": entrypoints}
		meta["openai/iconStyle"] = "monochrome" // jm's mark is one stroke color: the host tints it
	}
	return meta
}

// callableMeta marks a tool widgets may call themselves, with what the host
// says while it runs (none: "").
func callableMeta(invoking, invoked string) map[string]any {
	meta := map[string]any{"openai/widgetAccessible": true}
	if invoking != "" {
		meta["openai/toolInvocation/invoking"] = invoking
		meta["openai/toolInvocation/invoked"] = invoked
	}
	return meta
}

// entrypoint opens a widget's tool from the host's UI ({} as its arguments):
// "global" (the sidebar) or "thread" (a tab in a chat).
func entrypoint(kind string) map[string]any { return map[string]any{"type": kind} }

// fileEntrypoint opens files with these extensions (".scene.json") in the widget.
func fileEntrypoint(extensions ...string) map[string]any {
	return map[string]any{"type": "file", "extensions": extensions}
}

// resources lists w as both its kinds.
func (w widget) resources() []map[string]any {
	var out []map[string]any
	for _, uri := range []string{w.appURI(), w.legacyURI()} {
		mime, meta := w.kind(uri)
		out = append(out, map[string]any{"uri": uri, "name": strings.TrimSuffix(uri[len("ui://widget/"):], ".html"),
			"title": w.title, "description": w.description, "mimeType": mime, "_meta": meta})
	}
	return out
}

// read is w's page for uri; false: uri isn't one of w's.
func (w widget) read(uri string) (map[string]any, bool) {
	if uri != w.appURI() && uri != w.legacyURI() {
		return nil, false
	}
	mime, meta := w.kind(uri)
	page := strings.Replace(w.html, "/*__JM_BRIDGE__*/", widgetBridge, 1)
	page = strings.ReplaceAll(page, "__JM_VERSION__", version)
	return map[string]any{"uri": uri, "mimeType": mime, "_meta": meta, "text": page}, true
}

// kind is the MIME type and resource _meta of w served at uri.
func (w widget) kind(uri string) (string, map[string]any) {
	if uri == w.legacyURI() {
		return "text/html+skybridge", map[string]any{
			"openai/widgetDescription":   w.description,
			"openai/widgetPrefersBorder": true,
			"openai/widgetCSP":           map[string]any{"connect_domains": []string{}, "resource_domains": []string{}},
		}
	}
	return "text/html;profile=mcp-app", map[string]any{
		"ui":        map[string]any{"prefersBorder": true, "csp": map[string]any{"connectDomains": []string{}, "resourceDomains": []string{}}},
		"openai/ui": map[string]any{"availableDisplayModes": w.modes, "preferredDisplayMode": w.modes[0]},
	}
}

// widgets are every page jm serves.
var widgets = []widget{timeline, studio, sceneWidget}

func widgetResources() []map[string]any {
	var out []map[string]any
	for _, w := range widgets {
		out = append(out, w.resources()...)
	}
	return out
}

func readWidget(uri string) (map[string]any, bool) {
	for _, w := range widgets {
		if content, ok := w.read(uri); ok {
			return content, true
		}
	}
	return nil, false
}

// Journeyman's mark (a four-point star in a ring), one stroke color per theme:
// hosts show it beside jm's tools and entrypoints.
func serverIcons() []map[string]any {
	icon := func(theme string) map[string]any {
		return map[string]any{"src": "data:image/svg+xml;base64," + base64.StdEncoding.EncodeToString(plugin.Icon(theme)),
			"mimeType": "image/svg+xml", "sizes": []string{"any"}, "theme": theme}
	}
	return []map[string]any{icon("light"), icon("dark")}
}
