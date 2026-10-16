package main

import (
	"fmt"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"
)

// Variants (jm variant) as tools: try an idea in a variant instead of the
// game, compare it with the game at the same moment, then merge or drop it.
func (s *mcpServer) variantTools() []mcpTool {
	nameArg := strArg("the variant's name (variants lists them)")
	return []mcpTool{
		{Name: "variants", Meta: callableMeta("", ""), Title: "The game's variants", Description: "The open game's variants (jm variant): other takes on it, each a branch in its own folder " +
			"(open_game its dir to work in it), with how far it is from the game.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}),
			run: inRoot(func(root string, _ toolArgs) toolResult {
				list := variantsOf(root)
				var lines []string
				thumbs := map[string]string{}
				for _, v := range list {
					lines = append(lines, fmt.Sprintf("%s: %s (%s)", v.Name, v.Dir, v.summary()))
					if uri := dataURI(v.thumb, 320); uri != "" {
						thumbs[v.Name] = uri
					}
				}
				if len(lines) == 0 {
					lines = []string{"No variants: variant_new makes one."}
				}
				return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"variants": list, "branch": vcs.Branch(root)}, Meta: map[string]any{"jm/thumbs": thumbs}}
			})},
		{Name: "variant_new", Meta: callableMeta("Making the variant…", "The variant is made"), Title: "Make a variant",
			Description: "To try an idea without touching the game: a variant from its last commit (or a version), in its own folder. open_game its dir to work there; the game stays as it is.",
			Annotations: writes(), InputSchema: object(map[string]any{"name": strArg("e.g. floaty-jump (lowercase, digits, - and _)"),
				"from": strArg("a version from vcs_log (default: the last commit)")}, "name"),
			run: inRoot(func(root string, a toolArgs) toolResult {
				v, err := newVariant(root, a.str("name"), a.str("from"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "variant": v, "next": "open_game its dir to change it; variant_compare to see it beside the game"})
			})},
		{Name: "variant_play", Meta: callableMeta("Starting the variant…", "The variant is open"), Title: "Play a variant",
			Description: "Open a variant in its own window for the person to play (built first if it needs it).",
			Annotations: writes(), InputSchema: object(map[string]any{"name": nameArg}, "name"),
			run: inRoot(func(root string, a toolArgs) toolResult {
				v, err := findVariant(root, a.str("name"))
				if err == nil {
					err = playGame(v.Dir)
				}
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult("Variant "+v.Name+" is open for the person to play.", false)
			})},
		{Name: "variant_compare", Meta: callableMeta("Comparing…", "Compared"), Title: "Compare a variant",
			Description: "A variant beside the game (or another variant): the files their commits change (a scene's per entity) and a frame of each at the same moment, " +
				"both run headless with GL, with a recorded play's inputs if given. Show the person both frames.",
			Annotations: writes(), InputSchema: object(map[string]any{"a": nameArg, "b": strArg(`another variant, or "game" (default)`),
				"frame": intArg("the frame to show both at (default 120)"), "play": strArg("replay this recorded play's inputs in both (plays_list)")}, "a"),
			run: inRoot(func(root string, a toolArgs) toolResult {
				b := a.str("b")
				if b == "" {
					b = "game"
				}
				cmp, err := compareVariants(root, a.str("a"), b, int(a.num("frame", 120, 1, 1e6)), a.str("play"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				result := toolResult{Text: cmp.text(), Structured: map[string]any{"a": cmp.A, "b": cmp.B, "frame": cmp.Frame, "changes": cmp.Changes, "errors": cmp.Errors}}
				frames := []string{}
				for _, f := range cmp.Frames {
					uri := ""
					if f != "" {
						if data := jpegOf(f, 960); data != nil {
							result.Images = append(result.Images, mcpImage{data, "image/jpeg"})
							uri = dataURI(f, 640)
						}
					}
					frames = append(frames, uri)
				}
				result.Meta = map[string]any{"jm/frames": frames}
				return result
			})},
		{Name: "variant_merge", Meta: callableMeta("Merging…", "Merged"), Title: "Merge a variant",
			Description: "Bring a variant into the game once the person likes it: its uncommitted work is committed, then merged as a commit. A conflict merges nothing and names the files.",
			Annotations: writes(), InputSchema: object(map[string]any{"name": nameArg}, "name"),
			run: inRoot(func(root string, a toolArgs) toolResult {
				v, err := mergeVariant(root, a.str("name"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "merged": v, "next": "variant_drop it if it's done"})
			})},
		{Name: "variant_drop", Meta: callableMeta("Dropping…", "Dropped"), Title: "Drop a variant",
			Description: "Remove a variant's folder (its uncommitted work with it). Its branch is kept unless it was merged.",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": false},
			InputSchema: object(map[string]any{"name": nameArg}, "name"),
			run: inRoot(func(root string, a toolArgs) toolResult {
				kept, err := dropVariant(root, a.str("name"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult(dropText(a.str("name"), kept), false)
			})},
	}
}
