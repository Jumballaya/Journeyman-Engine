package main

import (
	"cmp"
	_ "embed"
	"encoding/base64"
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

var sceneFileArg = map[string]any{"type": "object", "description": "the scene file a host opened (its file entrypoint)", "properties": map[string]any{
	"name": map[string]any{"type": "string"}, "resourceUri": map[string]any{"type": "string"}}}

func (s *mcpServer) sceneTools() []mcpTool {
	return []mcpTool{{Name: "scene_view", Title: "Scene",
		Description: "Shows the person a scene: its entities laid out where they are (prefabs applied), to pick one and change it. " +
			"Gives you each entity's place, size and components.",
		Annotations: readOnly(), InputSchema: object(map[string]any{
			"scene": strArg("a scene file of the game, e.g. scenes/main.scene.json"),
			"file":  sceneFileArg,
		}),
		Meta:   sceneWidget.toolMeta("Opening the scene…", "The scene", fileEntrypoint(".scene.json")),
		onGame: sceneView},
		{Name: "scene_render", Title: "Render a scene",
			Description: "A scene of a game as the engine draws it, a moment after it starts (built first if the build is stale): " +
				"to see a change without the person playing. Scripts run; nothing is recorded.",
			Annotations: readOnly(), Meta: callableMeta("Rendering the scene…", "The scene"),
			InputSchema: object(map[string]any{"scene": strArg("a scene file of the game, e.g. scenes/main.scene.json"),
				"frame": intArg("the frame to draw (default 2: the scene as it starts)"), "file": sceneFileArg}),
			onGame: sceneRender}}
}

// sceneRender draws a scene in the engine (headless) and gives it as an image,
// and to a widget as a data URI (jm/image).
func sceneRender(root string, a toolArgs) toolResult {
	var engine string
	_, scene, err := scenePath(root, a)
	if err == nil && buildIsStale(root) {
		if out, failed := jmIn(a.call(), root, "build", "--json"); failed {
			err = fmt.Errorf("the build failed:\n%s", out)
		}
	}
	if err == nil {
		engine, err = resolveEnginePath()
	}
	if err == nil {
		engine, err = filepath.Abs(engine) // the run starts in build/
	}
	if err != nil {
		return textResult(err.Error(), true)
	}
	work, err := os.MkdirTemp("", "jm-render-")
	if err != nil {
		return textResult(err.Error(), true)
	}
	defer os.RemoveAll(work)
	frame := int(a.num("frame", 2, 0, 3600))
	if err := captureFrames(a.call().context(), engine, root, work, []int{frame}, "JM_ENTRY_SCENE="+filepath.ToSlash(scene)); err != nil {
		return textResult(err.Error(), true)
	}
	png := filepath.Join(work, "frames", fmt.Sprintf("frame_%05d.png", frame))
	jpeg := jpegOf(png, 960)
	if jpeg == nil {
		return textResult("the engine drew no frame (it needs a display to render)", true)
	}
	return toolResult{Text: fmt.Sprintf("%s as the engine draws it at frame %d.", filepath.ToSlash(scene), frame),
		Images: []mcpImage{{Data: jpeg, MimeType: "image/jpeg"}},
		Meta:   map[string]any{"jm/image": "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(jpeg)}}
}

// placedEntity is an entity where the scene puts it: x, y its center, w, h
// its full size (0: a point), components as the engine makes them (prefab, overrides).
type placedEntity struct {
	Where      string         `json:"where"` // e.g. entities[2].children[0]; a prefab's child: entities[2]/Shadow
	Name       string         `json:"name"`
	Prefab     string         `json:"prefab,omitempty"`
	Inherited  bool           `json:"inherited,omitempty"` // from a prefab: not in the scene file
	X          float64        `json:"x"`
	Y          float64        `json:"y"`
	W          float64        `json:"w"`
	H          float64        `json:"h"`
	Rotation   float64        `json:"rotation"`        // radians, counter-clockwise (y up), its parents' added
	Z          float64        `json:"z,omitempty"`     // draw order: higher on top
	FlipX      bool           `json:"flipX,omitempty"` // a negative scale mirrors it
	FlipY      bool           `json:"flipY,omitempty"`
	Look       string         `json:"look,omitempty"` // the texture its sprite shows as the scene starts
	Components map[string]any `json:"components"`
}

func sceneView(root string, a toolArgs) toolResult {
	path, rel, err := scenePath(root, a)
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
	placed := []placedEntity{}
	place(root, doc.Entities, nil, func(i int, _ string) string { return fmt.Sprintf("entities[%d]", i) }, false, placement{}, &placed, 0)
	names := []string{}
	for _, e := range placed[:min(len(placed), 40)] {
		names = append(names, e.Name)
	}
	text := fmt.Sprintf("Scene %s of %s: %d entities (%s). The person sees it laid out and can change an entity; structuredContent has each one's place and components.",
		rel, gameName(root), len(placed), strings.Join(names, ", "))
	out := map[string]any{"scene": rel, "game": root, "gameName": gameName(root), "name": doc.Name, "entities": placed}
	if s, ok := screenOf(root, placed); ok {
		out["screen"] = s
	}
	sprites, maps := scenePictures(root, placed)
	return toolResult{Text: text, Structured: out, Meta: map[string]any{"jm/sprites": sprites, "jm/maps": maps}}
}

// screenOf is what the game shows of a scene as it starts: its logical size
// (config.renderer, else the window's, by axis) about the shown camera (the
// highest priority, on its target if it follows one), else the origin.
func screenOf(root string, placed []placedEntity) (map[string]float64, bool) {
	var m struct {
		Config struct {
			Renderer struct{ LogicalWidth, LogicalHeight float64 }
			Window   struct{ Width, Height float64 }
		}
	}
	data, err := os.ReadFile(filepath.Join(root, archive.ManifestEntryKey))
	if err != nil || json.Unmarshal(data, &m) != nil {
		return nil, false
	}
	w, h := m.Config.Renderer.LogicalWidth, m.Config.Renderer.LogicalHeight
	if w <= 0 {
		w = m.Config.Window.Width
	}
	if h <= 0 {
		h = m.Config.Window.Height
	}
	if w <= 0 || h <= 0 {
		return nil, false
	}
	s := map[string]float64{"x": 0, "y": 0, "w": w, "h": h}
	var shown map[string]any
	best := math.Inf(-1)
	for _, e := range placed {
		_, placedSo := e.Components["TransformComponent"] // the engine's cameras have both
		if c, ok := e.Components["CameraComponent"].(map[string]any); ok && placedSo {
			if p, _ := c["priority"].(float64); p > best {
				best, shown = p, c
				s["x"], s["y"] = e.X, e.Y
			}
		}
	}
	if shown == nil { // the engine's default camera, until a script moves it
		return s, true
	}
	s["camera"] = 1 // a camera entity says where it looks
	zoom, _ := shown["zoom"].(float64)
	if zoom <= 0 {
		zoom = 1
	}
	s["w"], s["h"] = w/zoom, h/zoom
	// As the engine starts following: on the first entity of that name, if it's
	// placed, kept inside the first region it's in, else the bounds.
	if name, _ := shown["target"].(string); name != "" {
		for _, e := range placed {
			if e.Name != name {
				continue
			}
			if _, placedSo := e.Components["TransformComponent"]; placedSo {
				box, ok := cameraRect(shown["bounds"])
				regions, _ := shown["regions"].([]any)
				for _, r := range regions {
					if rect, isRect := cameraRect(r); isRect && rect[0] <= e.X && e.X <= rect[2] && rect[1] <= e.Y && e.Y <= rect[3] {
						box, ok = rect, true
						break
					}
				}
				s["x"], s["y"] = e.X, e.Y
				if ok {
					s["x"], s["y"] = keptInside(e.X, box[0], box[2], s["w"]/2), keptInside(e.Y, box[1], box[3], s["h"]/2)
				}
			}
			break
		}
	}
	return s, true
}

// cameraRect is a camera's {"min": [x, y], "max": [x, y]} as minX, minY, maxX,
// maxY (corners in either order).
func cameraRect(v any) ([4]float64, bool) {
	m, _ := v.(map[string]any)
	pair := func(key string) (float64, float64, bool) {
		p, _ := m[key].([]any)
		if len(p) != 2 {
			return 0, 0, false
		}
		x, okX := p[0].(float64)
		y, okY := p[1].(float64)
		return x, y, okX && okY
	}
	ax, ay, okA := pair("min")
	bx, by, okB := pair("max")
	return [4]float64{min(ax, bx), min(ay, by), max(ax, bx), max(ay, by)}, okA && okB
}

// keptInside is a center keeping a view half wide inside [lo, hi] (centered if it's wider).
func keptInside(center, lo, hi, half float64) float64 {
	if hi-lo <= half*2 {
		return (lo + hi) / 2
	}
	return max(lo+half, min(center, hi-half))
}

// scenePath is the scene a call means: the file a host opened, else a scene
// of the game named (game), else of the open game.
func scenePath(root string, a toolArgs) (path, rel string, err error) {
	scene := cmp.Or(a.resource(), a.str("scene"))
	if scene == "" {
		return "", "", fmt.Errorf("which scene? give scene, e.g. scenes/main.scene.json")
	}
	if path, rel, err = gamePath(root, scene); err != nil {
		return "", "", err
	}
	if !strings.HasSuffix(rel, ".scene.json") {
		return "", "", fmt.Errorf("%s isn't a scene (.scene.json)", filepath.Base(path))
	}
	if !exists(path) {
		return "", "", fmt.Errorf("%s: no such scene in the game", rel)
	}
	return path, rel, nil
}

// placement is where a parent is: children are placed from it, turned with it.
type placement struct{ x, y, z, rotation float64 }

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
		here := placement{parent.x + c*lx - sn*ly, parent.y + sn*lx + c*ly, parent.z + nth(pos, 2), parent.rotation + rotation}
		e.X, e.Y, e.Z, e.Rotation = here.x, here.y, here.z, here.rotation
		if len(scale) >= 2 { // scale is a half size
			e.W, e.H = 2*math.Abs(scale[0]), 2*math.Abs(scale[1])
			e.FlipX, e.FlipY = scale[0] < 0, scale[1] < 0
		}
		e.Look = spriteLook(e.Components)
		*out = append(*out, e)
		if t == nil { // as the engine: a parent without a transform holds its children nowhere
			here = placement{}
		}
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
	if real, ok := gameFile(root, prefab); ok {
		if data, err := os.ReadFile(real); err == nil {
			_ = json.Unmarshal(data, &p)
		}
	}
	return p
}

// gameFile is rel (a path from root) where it really is, links followed;
// false when that isn't inside the game at root.
func gameFile(root, rel string) (string, bool) {
	real, err := realPath(filepath.Join(root, filepath.FromSlash(rel)))
	rootReal, rootErr := realPath(root)
	inside, relErr := filepath.Rel(rootReal, real)
	return real, err == nil && rootErr == nil && relErr == nil && filepath.IsLocal(inside)
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
