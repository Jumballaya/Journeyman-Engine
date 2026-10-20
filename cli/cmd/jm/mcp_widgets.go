package main

import (
	"crypto/sha256"
	_ "embed"
	"encoding/base64"
	"encoding/hex"
	"strings"
	"sync"

	"github.com/Jumballaya/Journeyman-Engine/internal/plugin"
)

// Widgets: the HTML pages jm's tools show in hosts that render them. Each is
// served twice: as an MCP App (ui://…-app.html, the standard) and as ChatGPT's
// older skybridge widget. Pages get widget/bridge.js and the UI kit
// (widget/kit.js) inlined at the __JM_BRIDGE__ comment and this jm's version
// for __JM_VERSION__.

//go:embed widget/bridge.js
var widgetBridge string

//go:embed widget/kit.js
var widgetKit string

type widget struct {
	name, title, description string
	html                     string
	// Display modes the page works in, first preferred (ChatGPT opens
	// entrypoints fullscreen whatever this says).
	modes []string
}

// Its URIs name the page's content: hosts keep a page by its URI (ChatGPT
// until it restarts), so a jm with a changed page serves it at a new one.
func (w widget) appURI() string    { return "ui://widget/" + w.name + "-" + w.stamp() + "-app.html" }
func (w widget) legacyURI() string { return "ui://widget/" + w.name + "-" + w.stamp() + ".html" }

var stamps sync.Map // a widget's name → its page's stamp

func (w widget) stamp() string {
	if s, ok := stamps.Load(w.name); ok {
		return s.(string)
	}
	sum := sha256.Sum256([]byte(w.page()))
	s, _ := stamps.LoadOrStore(w.name, hex.EncodeToString(sum[:4]))
	return s.(string)
}

// page is w's HTML as served: the bridge, kit and version filled in.
func (w widget) page() string {
	page := strings.Replace(w.html, "/*__JM_BRIDGE__*/", widgetBridge+"\n"+widgetKit, 1)
	return strings.ReplaceAll(page, "__JM_VERSION__", version)
}

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
		mime, meta := w.kind(uri == w.legacyURI())
		out = append(out, map[string]any{"uri": uri, "name": strings.Replace(strings.TrimSuffix(uri[len("ui://widget/"):], ".html"), "-"+w.stamp(), "", 1),
			"title": w.title, "description": w.description, "mimeType": mime, "_meta": meta})
	}
	return out
}

// read is w's page for uri, at this stamp or another; false: uri isn't one of
// w's. A host that kept an older jm's URI (ChatGPT, until it restarts) gets
// this jm's page rather than nothing.
func (w widget) read(uri string) (map[string]any, bool) {
	legacy, ok := w.owns(uri)
	if !ok {
		return nil, false
	}
	mime, meta := w.kind(legacy)
	return map[string]any{"uri": uri, "mimeType": mime, "_meta": meta, "text": w.page()}, true
}

// owns says whether uri is w's at any stamp, and whether it's the legacy kind.
func (w widget) owns(uri string) (legacy, ok bool) {
	rest, found := strings.CutPrefix(uri, "ui://widget/"+w.name+"-")
	if !found || len(rest) < 8 || strings.Trim(rest[:8], "0123456789abcdef") != "" {
		return false, false
	}
	switch rest[8:] {
	case ".html":
		return true, true
	case "-app.html":
		return false, true
	}
	return false, false
}

// kind is the MIME type and resource _meta of w served as the legacy kind or not.
func (w widget) kind(legacy bool) (string, map[string]any) {
	if legacy {
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
