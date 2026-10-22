package main

import (
	"cmp"
	_ "embed"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// A scene file as a page: ChatGPT desktop opens .scene.json files in it (a
// file entrypoint). The page reads and writes the file through the host
// (openai/resources/write); this tool lays the scene out as the engine builds it (inspect).

//go:embed widget/scene.html
var scenePage string

var sceneWidget = widget{name: "journeyman-scene", title: "Scene", html: scenePage, modes: []string{"fullscreen", "inline"},
	description: "A scene of the game: its entities where they are, and an inspector to change one."}

var sceneFileArg = map[string]any{"type": "object", "description": "the scene file a host opened (its file entrypoint)", "properties": map[string]any{
	"name": map[string]any{"type": "string"}, "resourceUri": map[string]any{"type": "string"}}}

func (s *mcpServer) sceneTools() []mcpTool {
	return []mcpTool{{Name: "scene_view", Title: "Scene",
		Description: "Shows the person a scene: its entities as the engine builds it (prefabs, overrides, children, the camera; " +
			"built first if the build is stale), to pick one and change it. Gives you each entity's place, size and components.",
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
	if err == nil {
		err = freshBuild(a.call(), root)
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

// placedEntity is an entity where the engine puts it: x, y its center, w, h
// its full size (0: a point), components as it makes them (prefab, overrides).
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
	var doc struct {
		Name string `json:"name"`
	}
	if data, err := os.ReadFile(path); err != nil || json.Unmarshal(data, &doc) != nil {
		return textResult(fmt.Sprintf("%s isn't a scene: it isn't JSON", filepath.Base(path)), true)
	}
	seen, err := inspect(a.call(), root, rel)
	if err != nil {
		return textResult(err.Error(), true)
	}
	placed := seen.placed()
	names := []string{}
	for _, e := range placed[:min(len(placed), 40)] {
		names = append(names, e.Name)
	}
	text := fmt.Sprintf("Scene %s of %s: %d entities (%s). The person sees it laid out and can change an entity; structuredContent has each one's place and components.",
		rel, gameName(root), len(placed), strings.Join(names, ", "))
	out := map[string]any{"scene": rel, "game": root, "gameName": gameName(root), "name": doc.Name, "entities": placed}
	if seen.Error != "" {
		text += " The engine couldn't load it: " + seen.Error
		out["error"] = seen.Error
	}
	if s, ok := seen.screen(); ok {
		out["screen"] = s
	}
	sprites, maps := scenePictures(root, placed)
	return toolResult{Text: text, Structured: out, Meta: map[string]any{"jm/sprites": sprites, "jm/maps": maps}}
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
