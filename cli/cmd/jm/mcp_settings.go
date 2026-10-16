package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/filelock"
	"github.com/Jumballaya/Journeyman-Engine/internal/jsonfmt"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// Settings: the open game's .jm.json and the editor's preferences as one flat
// list of typed fields. ChatGPT draws them on its plugin page (openai/settings);
// any agent reads and changes them with the same two tools.

const (
	settingsReadTool   = "settings_read"
	settingsUpdateTool = "settings_update"
)

// A setting's key is where it lives: "editor.<key>" is the editor's
// preferences.json <key>, anything else a dotted path in .jm.json.
type setting struct {
	key, group string
	schema     map[string]any
	fallback   any // the value when the file doesn't say
}

const editorPrefix = "editor."

func (s setting) inEditor() bool { return strings.HasPrefix(s.key, editorPrefix) }

func boolSetting(key, group, title, description string, fallback bool) setting {
	return setting{key, group, map[string]any{"type": "boolean", "title": title, "description": description}, fallback}
}

func intSetting(key, group, title, description string, lo, hi, fallback int) setting {
	return setting{key, group, map[string]any{"type": "integer", "title": title, "description": description, "minimum": lo, "maximum": hi}, fallback}
}

func strSetting(key, group, title, description string, choices ...string) setting {
	schema := map[string]any{"type": "string", "title": title, "description": description}
	fallback := ""
	if len(choices) > 0 {
		schema["enum"], fallback = choices, choices[0]
	}
	return setting{key, group, schema, fallback}
}

// settingsGroups are in the order the page shows them.
var settingsGroups = []string{"Game", "Window", "Picture", "Editor", "Export", "Games"}

// gameSettings are the open game's: the engine reads them when the game starts.
func gameSettings(scenes []string) []setting {
	name := strSetting("name", "Game", "Name", "The game's name: its window title and exported app's name.")
	name.schema["minLength"] = 1
	return []setting{
		name,
		strSetting("version", "Game", "Version", "The game's version, e.g. 0.1.0."),
		strSetting("entryScene", "Game", "Start scene", "The scene the game opens on.", scenes...),
		intSetting("config.window.width", "Window", "Width", "The window's width in pixels.", 64, 8192, 1280),
		intSetting("config.window.height", "Window", "Height", "The window's height in pixels.", 64, 8192, 720),
		boolSetting("config.window.resizable", "Window", "Resizable", "The player can resize the window.", true),
		boolSetting("config.window.fullscreen", "Window", "Fullscreen", "Start fullscreen.", false),
		boolSetting("config.window.vsync", "Window", "Vsync", "Wait for the display between frames.", true),
		boolSetting("config.window.hideCursor", "Window", "Hide cursor", "Hide the mouse cursor over the game.", false),
		intSetting("config.renderer.logicalWidth", "Picture", "Logical width", "Draw at this width and scale it to the window (pixel art); 0: the window's.", 0, 8192, 0),
		intSetting("config.renderer.logicalHeight", "Picture", "Logical height", "Draw at this height and scale it to the window; 0: the window's.", 0, 8192, 0),
	}
}

// editorSettings take effect when the editor next starts.
var editorSettings = []setting{
	strSetting("editor.appearance", "Editor", "Appearance", "The editor's colors: the system's, light or dark.", "system", "light", "dark"),
	strSetting("editor.codeEditor", "Editor", "Script editor", "The app Edit Script opens scripts in (ask: choose each time).", "ask", "vscode", "cursor", "zed", "sublime", "text"),
	strSetting("editor.exportSignIdentity", "Export", "Signing identity", "The Developer ID the editor's Export signs macOS apps with (empty: none)."),
	strSetting("editor.exportNotarizeProfile", "Export", "Notary profile", "The notarytool keychain profile Export notarizes with."),
	boolSetting("editor.exportNotarize", "Export", "Notarize", "Export sends signed apps to Apple to notarize.", false),
}

// settingsActions are buttons on the page: tools that take {} (the open game).
var settingsActions = []string{"games", "project_play", "project_open_editor"}

// settingsFile is one file settings live in, parsed: a missing one is empty.
type settingsFile struct {
	path string
	raw  []byte
	doc  map[string]any
}

func loadSettingsFile(path string) (*settingsFile, error) {
	f := &settingsFile{path: path, doc: map[string]any{}}
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return f, nil
	}
	if err == nil && (decodeExactly(data, &f.doc) != nil || f.doc == nil) {
		err = fmt.Errorf("%s isn't a JSON object", path)
	}
	f.raw = data
	return f, err
}

// decodeExactly is json.Unmarshal with numbers kept as their literals
// (json.Number), so ones a save doesn't touch come back as they were.
func decodeExactly(data []byte, v any) error {
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	if err := dec.Decode(v); err != nil {
		return err
	}
	if _, err := dec.Token(); err != io.EOF {
		return errors.New("more after the value")
	}
	return nil
}

// settingsState is every setting there is now and the files they're in.
type settingsState struct {
	list            []setting
	manifest, prefs *settingsFile // manifest nil: no game open
}

// holdSettings holds the files set changes against every other jm and the
// editor's preference writes: preferences first, then .jm.json (all take that order).
func holdSettings(set map[string]any) (release func(), err error) {
	var files []string
	for key := range set {
		if strings.HasPrefix(key, editorPrefix) {
			files = append(files, projects.PreferencesFile())
			break
		}
	}
	for key := range set {
		if !strings.HasPrefix(key, editorPrefix) && exists(archive.ManifestEntryKey) {
			abs, _ := filepath.Abs(archive.ManifestEntryKey)
			files = append(files, abs)
			break
		}
	}
	var held []func()
	release = func() {
		for i := len(held) - 1; i >= 0; i-- {
			held[i]()
		}
	}
	for _, f := range files {
		r, err := filelock.Hold(f)
		if err != nil {
			release()
			return nil, err
		}
		held = append(held, r)
	}
	return release, nil
}

func loadSettings() (*settingsState, error) {
	st := &settingsState{}
	var err error
	if st.prefs, err = loadSettingsFile(projects.PreferencesFile()); err != nil {
		return nil, err
	}
	if exists(archive.ManifestEntryKey) {
		abs, _ := filepath.Abs(archive.ManifestEntryKey)
		if st.manifest, err = loadSettingsFile(abs); err != nil {
			return nil, err
		}
		scenes := []string{}
		for _, s := range asList(st.manifest.doc["scenes"]) {
			scenes = append(scenes, fmt.Sprint(s))
		}
		if entry, _ := st.manifest.doc["entryScene"].(string); entry != "" && !contains(scenes, entry) {
			scenes = append([]string{entry}, scenes...)
		}
		st.list = gameSettings(scenes)
	}
	st.list = append(st.list, editorSettings...)
	return st, nil
}

func (st *settingsState) find(key string) (setting, bool) {
	for _, s := range st.list {
		if s.key == key {
			return s, true
		}
	}
	return setting{}, false
}

// value is s as its file has it, else s's fallback. Preferences are strings
// ("" for the fallback, "yes" for true), as the editor keeps them.
func (st *settingsState) value(s setting) any {
	if s.inEditor() {
		pref, _ := st.prefs.doc[strings.TrimPrefix(s.key, editorPrefix)].(string)
		switch {
		case pref == "":
			return s.fallback
		case s.schema["type"] == "boolean":
			return pref == "yes"
		}
		return pref
	}
	var at any = st.manifest.doc
	for _, part := range strings.Split(s.key, ".") {
		obj, _ := at.(map[string]any)
		if at = obj[part]; at == nil {
			return s.fallback
		}
	}
	if n, ok := at.(json.Number); ok && s.schema["type"] == "integer" {
		if f, err := n.Float64(); err == nil && f == math.Trunc(f) { // 960 or 960.0
			return int(f)
		}
	}
	return at
}

func (st *settingsState) set(s setting, v any) {
	if s.inEditor() {
		pref := fmt.Sprint(v)
		switch {
		case v == s.fallback:
			pref = ""
		case v == true:
			pref = "yes"
		}
		st.prefs.doc[strings.TrimPrefix(s.key, editorPrefix)] = pref
		return
	}
	st.manifest.setPath(s.key, v)
}

// setPath sets the value at a dotted path, making the objects on the way.
func (f *settingsFile) setPath(path string, v any) {
	obj := f.doc
	parts := strings.Split(path, ".")
	for _, part := range parts[:len(parts)-1] {
		next, ok := obj[part].(map[string]any)
		if !ok {
			next = map[string]any{}
			obj[part] = next
		}
		obj = next
	}
	obj[parts[len(parts)-1]] = v
}

func (st *settingsState) values() map[string]any {
	out := map[string]any{}
	for _, s := range st.list {
		out[s.key] = st.value(s)
	}
	return out
}

// read is the settings_read result: ChatGPT's SettingsReadResult, and the
// same as text for the model.
func (st *settingsState) read() toolResult {
	properties := map[string]any{}
	for _, s := range st.list {
		properties[s.key] = s.schema
	}
	var layout []map[string]any
	var b strings.Builder
	if st.manifest == nil {
		b.WriteString("No game open: open_game for its settings.\n")
	}
	for _, group := range settingsGroups {
		var items []map[string]any
		for _, s := range st.list {
			if s.group == group {
				items = append(items, map[string]any{"kind": "property", "property": s.key})
				value, _ := json.Marshal(st.value(s))
				fmt.Fprintf(&b, "%s %s (%s): %s\n", group, s.schema["title"], s.key, value)
			}
		}
		if group == "Games" {
			for _, tool := range settingsActions {
				items = append(items, map[string]any{"kind": "tool", "tool": tool})
			}
		}
		if len(items) > 0 {
			layout = append(layout, map[string]any{"kind": "group", "title": group, "items": items})
		}
	}
	b.WriteString("Change them with settings_update {set: {key: value}}. Editor settings apply when the editor next starts.")
	return toolResult{Text: b.String(), Structured: map[string]any{
		"schema": map[string]any{"type": "object", "properties": properties}, "values": st.values(), "layout": layout}}
}

// update applies set (none of it when one value is wrong) and saves
// the files it changed.
func (st *settingsState) update(set map[string]any) toolResult {
	if len(set) == 0 {
		return textResult(`Nothing to change: set {key: value}, keys from settings_read.`, true)
	}
	keys := make([]string, 0, len(set))
	for key := range set {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	changed := map[*settingsFile]bool{}
	for _, key := range keys {
		s, ok := st.find(key)
		if !ok {
			if st.manifest == nil && !strings.HasPrefix(key, editorPrefix) {
				return textResult("No game open, so no "+key+": open_game first.", true)
			}
			return textResult("No setting "+key+" (settings_read lists them).", true)
		}
		v, err := checkSetting(s.schema, set[key])
		if err != nil {
			return textResult(fmt.Sprintf("%s: %v", key, err), true)
		}
		st.set(s, v)
		changed[st.fileOf(s)] = true
	}
	for f := range changed {
		if err := f.save(); err != nil {
			return textResult(err.Error(), true)
		}
	}
	return toolResult{Text: "Saved " + strings.Join(keys, ", ") + ".", Structured: map[string]any{"values": st.values()}}
}

func (st *settingsState) fileOf(s setting) *settingsFile {
	if s.inEditor() {
		return st.prefs
	}
	return st.manifest
}

// save writes the file in the layout jm and the editor share, its keys in the order they were.
func (f *settingsFile) save() error {
	data, err := json.Marshal(f.doc)
	if err == nil {
		data, err = jsonfmt.FormatKeeping(data, f.raw)
	}
	if err == nil {
		err = os.MkdirAll(filepath.Dir(f.path), 0o755)
	}
	if err == nil {
		err = atomicfile.WriteFile(f.path, data, 0o644)
	}
	return err
}

// checkSetting is v if it fits schema (a whole float as an integer), else why not.
func checkSetting(schema map[string]any, v any) (any, error) {
	switch schema["type"] {
	case "boolean":
		if _, ok := v.(bool); !ok {
			return nil, errors.New("true or false")
		}
	case "integer":
		n, ok := v.(float64)
		if !ok || n != math.Trunc(n) {
			return nil, errors.New("a whole number")
		}
		if n < float64(schema["minimum"].(int)) || n > float64(schema["maximum"].(int)) {
			return nil, fmt.Errorf("from %v to %v", schema["minimum"], schema["maximum"])
		}
		return int(n), nil
	case "string":
		s, ok := v.(string)
		if !ok {
			return nil, errors.New("text")
		}
		if choices, ok := schema["enum"].([]string); ok && !contains(choices, s) {
			return nil, errors.New("one of " + strings.Join(choices, ", "))
		}
		if min, ok := schema["minLength"].(int); ok && len(s) < min {
			return nil, errors.New("can't be empty")
		}
		if p, ok := schema["pattern"].(string); ok && !regexp.MustCompile(p).MatchString(s) {
			return nil, errors.New("must match " + p)
		}
	}
	return v, nil
}

func contains(list []string, s string) bool {
	for _, item := range list {
		if item == s {
			return true
		}
	}
	return false
}

func asList(v any) []any {
	list, _ := v.([]any)
	return list
}

func settingsTools() []mcpTool {
	return []mcpTool{
		{Name: settingsReadTool, Title: "Journeyman settings", Description: "The open game's settings (name, start scene, window, logical size: .jm.json) and the editor's preferences, " +
			"each with its key, type and current value. ChatGPT shows them on Journeyman's settings page.",
			Annotations: readOnly(), InputSchema: map[string]any{"type": "object", "additionalProperties": false},
			OutputSchema: object(map[string]any{"schema": map[string]any{"type": "object"}, "values": map[string]any{"type": "object"},
				"layout": map[string]any{"type": "array", "items": map[string]any{"type": "object"}}}, "schema", "values"),
			run: func(toolArgs) toolResult {
				st, err := loadSettings()
				if err != nil {
					return textResult(err.Error(), true)
				}
				return st.read()
			}},
		{Name: settingsUpdateTool, Title: "Change settings", Description: "Change settings by key (from settings_read), e.g. {set: {\"config.window.width\": 960}}: written to .jm.json or the editor's preferences. One wrong value and nothing changes.",
			Annotations: writes(), InputSchema: map[string]any{"type": "object", "additionalProperties": false, "required": []string{"set"},
				"properties": map[string]any{"set": map[string]any{"type": "object", "minProperties": 1, "description": "setting key -> new value"}}},
			OutputSchema: object(map[string]any{"values": map[string]any{"type": "object"}}, "values"),
			run: func(a toolArgs) toolResult {
				set, _ := a["set"].(map[string]any)
				release, err := holdSettings(set)
				if err != nil {
					return textResult(err.Error(), true)
				}
				defer release()
				st, err := loadSettings()
				if err != nil {
					return textResult(err.Error(), true)
				}
				return st.update(set)
			}},
	}
}
