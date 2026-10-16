package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"math"
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
// its size (0: a point), components as the engine makes them (prefab, overrides).
type placedEntity struct {
	Where      string         `json:"where"` // e.g. entities[2].children[0]; a prefab's child: entities[2]/Shadow
	Name       string         `json:"name"`
	Prefab     string         `json:"prefab,omitempty"`
	Inherited  bool           `json:"inherited,omitempty"` // from a prefab: not in the scene file
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
	place(root, doc.Entities, nil, func(i int, _ string) string { return fmt.Sprintf("entities[%d]", i) }, false, placement{}, &placed, 0)
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

// placement is where a parent is: children are placed from it, turned with it.
type placement struct{ x, y, rotation float64 }

// place lays out entries as the engine's SceneLoader makes them, into out:
// each one's prefab under its overrides, and its children (the prefab's, with
// overrides naming them, then its own) placed from it. childOverrides are a
// prefab instance's changes to its prefab's children, by name.
func place(root string, entries []map[string]any, childOverrides map[string]any, whereOf func(i int, name string) string,
	inherited bool, parent placement, out *[]placedEntity, depth int) {
	if depth > 32 { // as the engine: a prefab that holds itself
		return
	}
	for i, entry := range entries {
		e := placedEntity{Inherited: inherited, Components: map[string]any{}}
		e.Name, _ = entry["name"].(string)
		e.Where = whereOf(i, e.Name)
		e.Prefab, _ = entry["prefab"].(string)
		changes, _ := childOverrides[e.Name].(map[string]any)
		var prefabChildren []map[string]any
		var overrides map[string]any
		if e.Prefab != "" {
			p := readPrefab(root, e.Prefab)
			prefabChildren = p.Children
			own, _ := entry["overrides"].(map[string]any)
			overrides, _ = mergePatch(own, changes).(map[string]any)
			for name, c := range p.Components {
				e.Components[name] = c
			}
			for name, c := range overrides {
				if name != "children" {
					e.Components[name] = mergeDeep(e.Components[name], c)
				}
			}
		} else {
			own, _ := entry["components"].(map[string]any)
			e.Components, _ = mergePatch(own, changes).(map[string]any)
		}
		t, _ := e.Components["TransformComponent"].(map[string]any)
		pos, scale := numbers(t["position"]), numbers(t["scale"])
		rotation, _ := t["rotation"].(float64)
		lx, ly := nth(pos, 0), nth(pos, 1)
		c, sn := math.Cos(parent.rotation), math.Sin(parent.rotation)
		here := placement{parent.x + c*lx - sn*ly, parent.y + sn*lx + c*ly, parent.rotation + rotation}
		e.X, e.Y = here.x, here.y
		if len(scale) >= 2 {
			e.W, e.H = scale[0], scale[1]
		}
		*out = append(*out, e)
		named, _ := overrides["children"].(map[string]any)
		place(root, prefabChildren, named, func(_ int, name string) string { return e.Where + "/" + name }, true, here, out, depth+1)
		place(root, childrenOf(entry), nil, func(j int, _ string) string { return fmt.Sprintf("%s.children[%d]", e.Where, j) },
			inherited, here, out, depth+1)
	}
}

// mergeDeep is the engine's override merge: objects key by key, anything
// else replaced. Neither argument changes.
func mergeDeep(base, changes any) any {
	c, ok := changes.(map[string]any)
	if !ok {
		return changes
	}
	out := map[string]any{}
	if b, ok := base.(map[string]any); ok {
		for k, v := range b {
			out[k] = v
		}
	}
	for k, v := range c {
		out[k] = mergeDeep(out[k], v)
	}
	return out
}

// mergePatch is changes applied as the engine's child changes are (RFC 7396,
// nlohmann's merge_patch): mergeDeep, but a null takes the key away.
func mergePatch(base, changes any) any {
	c, ok := changes.(map[string]any)
	if !ok {
		return changes
	}
	out := map[string]any{}
	if b, ok := base.(map[string]any); ok {
		for k, v := range b {
			out[k] = v
		}
	}
	for k, v := range c {
		if v == nil {
			delete(out, k)
		} else {
			out[k] = mergePatch(out[k], v)
		}
	}
	return out
}

type prefabFile struct {
	Components map[string]any   `json:"components"`
	Children   []map[string]any `json:"children"`
}

// readPrefab is a prefab of the game; empty when it can't be read or isn't
// the game's own file.
func readPrefab(root, prefab string) prefabFile {
	var p prefabFile
	path := filepath.Join(root, filepath.FromSlash(prefab))
	real, err := realPath(path)
	rootReal, rootErr := realPath(root)
	if rel, relErr := filepath.Rel(rootReal, real); err != nil || rootErr != nil || relErr != nil || !filepath.IsLocal(rel) {
		return p
	}
	if data, err := os.ReadFile(real); err == nil {
		_ = json.Unmarshal(data, &p)
	}
	return p
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
