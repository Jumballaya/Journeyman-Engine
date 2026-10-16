package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
)

// A scene file as a page: ChatGPT desktop opens .scene.json files in it (a
// file entrypoint). The page reads and writes the file through the host
// (openai/resources/write); this tool lays the scene out, prefabs applied.

//go:embed widget/scene.html
var scenePage string

var sceneWidget = widget{name: "journeyman-scene", title: "Scene", html: scenePage, modes: []string{"fullscreen", "inline"},
	description: "A scene of the game: its entities where they are, and an inspector to change one."}

func (s *mcpServer) sceneTools() []mcpTool {
	return []mcpTool{{Name: "scene_view", Title: "Scene",
		Description: "Shows the person a scene: its entities laid out where they are (prefabs applied), to pick one and change it. " +
			"Gives you each entity's place, size and components.",
		Annotations: readOnly(), InputSchema: object(map[string]any{
			"scene": strArg("a scene file of the open game, e.g. scenes/main.scene.json"),
			"file": map[string]any{"type": "object", "description": "the scene file a host opened (its file entrypoint)", "properties": map[string]any{
				"name": map[string]any{"type": "string"}, "resourceUri": map[string]any{"type": "string"}}},
		}),
		Meta: sceneWidget.toolMeta("Opening the scene…", "The scene", fileEntrypoint(".scene.json")),
		run:  sceneView}}
}

// placedEntity is an entity where the scene puts it: x, y its center, w, h
// its size (0: a point), components with its prefab's under its own.
type placedEntity struct {
	Where      string         `json:"where"` // e.g. entities[2].children[0]
	Name       string         `json:"name"`
	Prefab     string         `json:"prefab,omitempty"`
	X          float64        `json:"x"`
	Y          float64        `json:"y"`
	W          float64        `json:"w"`
	H          float64        `json:"h"`
	Components map[string]any `json:"components"`
}

func sceneView(a toolArgs) toolResult {
	path, err := scenePath(a)
	if err != nil {
		return textResult(err.Error(), true)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return textResult(err.Error(), true)
	}
	var doc struct {
		Name     string           `json:"name"`
		Entities []map[string]any `json:"entities"`
	}
	if err := json.Unmarshal(data, &doc); err != nil {
		return textResult(fmt.Sprintf("%s isn't a scene: %v", filepath.Base(path), err), true)
	}
	root := gameRootOf(path)
	rel, _ := filepath.Rel(root, path)
	placed := []placedEntity{}
	place(root, doc.Entities, "entities", 0, 0, &placed)
	names := []string{}
	for _, e := range placed[:min(len(placed), 40)] {
		names = append(names, e.Name)
	}
	text := fmt.Sprintf("Scene %s of %s: %d entities (%s). The person sees it laid out and can change an entity; structuredContent has each one's place and components.",
		filepath.ToSlash(rel), gameName(root), len(placed), strings.Join(names, ", "))
	return toolResult{Text: text, Structured: map[string]any{"scene": filepath.ToSlash(rel), "game": gameName(root), "name": doc.Name, "entities": placed}}
}

// scenePath is the scene a call means: the file a host opened, else a scene
// of the open game.
func scenePath(a toolArgs) (string, error) {
	path := a.resource()
	if path == "" {
		if a.str("scene") == "" {
			return "", fmt.Errorf("which scene? give scene, e.g. scenes/main.scene.json")
		}
		rel, err := projectFile(a.str("scene"))
		if err != nil {
			return "", err
		}
		path, _ = filepath.Abs(rel)
	}
	if !strings.HasSuffix(path, ".scene.json") {
		return "", fmt.Errorf("%s isn't a scene (.scene.json)", filepath.Base(path))
	}
	return path, nil
}

// gameRootOf is the game folder holding path (the nearest with a .jm.json),
// else path's own folder.
func gameRootOf(path string) string {
	for dir := filepath.Dir(path); ; dir = filepath.Dir(dir) {
		if exists(filepath.Join(dir, archive.ManifestEntryKey)) {
			return dir
		}
		if filepath.Dir(dir) == dir {
			return filepath.Dir(path)
		}
	}
}

// place lays out entities (children from their parent's place) into out.
func place(root string, entities []map[string]any, where string, ox, oy float64, out *[]placedEntity) {
	for i, e := range entities {
		at := fmt.Sprintf("%s[%d]", where, i)
		p := placedEntity{Where: at, Components: map[string]any{}}
		p.Name, _ = e["name"].(string)
		p.Prefab, _ = e["prefab"].(string)
		if p.Prefab != "" {
			overlay(p.Components, prefabComponents(root, p.Prefab))
		}
		own, _ := e["components"].(map[string]any)
		overrides, _ := e["overrides"].(map[string]any)
		overlay(p.Components, own)
		overlay(p.Components, overrides)
		t, _ := p.Components["TransformComponent"].(map[string]any)
		pos, scale := numbers(t["position"]), numbers(t["scale"])
		p.X, p.Y = ox+nth(pos, 0), oy+nth(pos, 1)
		if len(scale) >= 2 {
			p.W, p.H = scale[0], scale[1]
		}
		*out = append(*out, p)
		place(root, childrenOf(e), at+".children", p.X, p.Y, out)
	}
}

// overlay puts each of from's components over into's, key by key (a
// prefab instance's overrides change only the keys they name).
func overlay(into, from map[string]any) {
	for name, c := range from {
		values, ok := c.(map[string]any)
		base, isMap := into[name].(map[string]any)
		if !ok || !isMap {
			into[name] = c
			continue
		}
		merged := map[string]any{}
		for k, v := range base {
			merged[k] = v
		}
		for k, v := range values {
			merged[k] = v
		}
		into[name] = merged
	}
}

// prefabComponents are a prefab's components; none when it can't be read or
// isn't the game's own file.
func prefabComponents(root, prefab string) map[string]any {
	path := filepath.Join(root, filepath.FromSlash(prefab))
	real, err := realPath(path)
	rootReal, rootErr := realPath(root)
	if rel, relErr := filepath.Rel(rootReal, real); err != nil || rootErr != nil || relErr != nil || !filepath.IsLocal(rel) {
		return nil
	}
	var p struct {
		Components map[string]any `json:"components"`
	}
	if data, err := os.ReadFile(real); err == nil {
		_ = json.Unmarshal(data, &p)
	}
	return p.Components
}

func numbers(v any) []float64 {
	list, _ := v.([]any)
	out := []float64{}
	for _, item := range list {
		n, _ := item.(float64)
		out = append(out, n)
	}
	return out
}

func nth(list []float64, i int) float64 {
	if i < len(list) {
		return list[i]
	}
	return 0
}
