package main

import (
	"cmp"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"image"
	"maps"
	"os"
	"path"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
)

// A game file shown in Journeyman Studio (its File tab): ChatGPT desktop opens
// prefabs, atlases and key bindings in it (a file entrypoint).
func (s *mcpServer) fileViewTools() []mcpTool {
	home := gamesHome()
	return []mcpTool{{Name: "file_view", Title: "Journeyman file",
		Description: "Shows the person a file of the game in Journeyman Studio: a prefab (its entities and components), " +
			"an atlas (its images, by region name) or key bindings (each action's keys, which they can change there). " +
			"Gives you the same.",
		Annotations: readOnly(),
		InputSchema: object(map[string]any{
			"path": strArg("a file of the game: a .prefab.json, .atlas.json or .bindings.json"),
			"file": map[string]any{"type": "object", "description": "the file a host opened (its file entrypoint)", "properties": map[string]any{
				"name": map[string]any{"type": "string"}, "resourceUri": map[string]any{"type": "string"}}},
		}),
		Meta: studio.toolMeta("Opening the file…", "The file", fileEntrypoint(".prefab.json", ".atlas.json", ".bindings.json")),
		onGame: func(root string, a toolArgs) toolResult {
			file, summary, thumbs, err := viewFile(root, a)
			if err != nil {
				return textResult(err.Error(), true)
			}
			r := s.listGames(home) // the Studio around it
			r.Structured.(map[string]any)["file"] = file
			r.Meta["jm/files"] = thumbs
			r.Text = summary
			return r
		}}}
}

// viewFile reads the file of the game at root a call means (the one a host
// opened, else path) as its kind shows it, with pictures of an atlas's images by path.
func viewFile(root string, a toolArgs) (file map[string]any, summary string, thumbs map[string]string, err error) {
	abs, rel, err := gamePath(root, cmp.Or(a.resource(), a.str("path")))
	if err != nil {
		return nil, "", nil, err
	}
	data, err := os.ReadFile(abs)
	if err != nil {
		return nil, "", nil, fmt.Errorf("%s: no such file in %s", rel, gameName(root))
	}
	// game: its folder, which a page tells games apart by (names can be alike).
	file = map[string]any{"path": rel, "game": root, "gameName": gameName(root)}
	thumbs = map[string]string{}
	name := path.Base(rel)
	switch {
	case strings.HasSuffix(rel, ".prefab.json"):
		placed := []placedEntity{}
		stem := strings.TrimSuffix(name, ".prefab.json")
		place(root, []map[string]any{{"name": stem, "prefab": rel}}, nil, func(int, string) string { return stem }, false, placement{}, &placed, 0)
		file["kind"], file["entities"] = "prefab", placed
		summary = fmt.Sprintf("The prefab %s of %s: %d entities (itself and its children); structuredContent has their components.", rel, gameName(root), len(placed))
	case strings.HasSuffix(rel, ".atlas.json"):
		var config atlas.AtlasConfig
		if err := json.Unmarshal(data, &config); err != nil {
			return nil, "", nil, fmt.Errorf("%s isn't an atlas: %v", name, err)
		}
		images := []map[string]any{}
		add := func(source, region string, img image.Image) {
			key := source + "#" + region
			b := img.Bounds()
			images = append(images, map[string]any{"path": key, "region": region, "size": fmt.Sprintf("%d×%d", b.Dx(), b.Dy())})
			if len(thumbs) < 400 {
				if jpeg := jpegImage(img, 96); jpeg != nil {
					thumbs[key] = "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(jpeg)
				}
			}
		}
		for _, p := range config.Sources {
			source := atlas.SourceOf(p)
			frames, _, err := source.Read(gameReader(root), pictureSideMax)
			if err != nil {
				images = append(images, map[string]any{"path": p, "region": source.Name, "size": err.Error()})
			}
			for _, f := range frames {
				add(p, f.Name, f.Img)
			}
		}
		file["kind"], file["images"], file["filter"], file["padding"] = "atlas", images, config.Filter, config.Padding
		summary = fmt.Sprintf("The atlas %s of %s: %d images, named %s#<region>.", rel, gameName(root), len(images), rel)
	case strings.HasSuffix(rel, ".bindings.json"):
		var bindings struct {
			Actions map[string][]string `json:"actions"`
		}
		if err := json.Unmarshal(data, &bindings); err != nil || bindings.Actions == nil {
			return nil, "", nil, fmt.Errorf("%s isn't key bindings (an \"actions\" object): %v", name, err)
		}
		file["kind"], file["actions"] = "bindings", bindings.Actions
		var lines []string
		for _, action := range slices.Sorted(maps.Keys(bindings.Actions)) {
			keys := bindings.Actions[action]
			lines = append(lines, action+": "+strings.Join(keys, ", "))
		}
		summary = fmt.Sprintf("The key bindings %s of %s (scripts read them as Input actions):\n%s", rel, gameName(root), strings.Join(lines, "\n"))
	default:
		return nil, "", nil, fmt.Errorf("%s: file_view shows prefabs, atlases and key bindings (scene_view shows scenes)", name)
	}
	return file, summary, thumbs, nil
}
