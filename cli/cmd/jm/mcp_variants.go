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
			onGame: func(root string, _ toolArgs) toolResult {
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
			}},
		{Name: "variant_new", Meta: callableMeta("Making the variant…", "The variant is made"), Title: "Make a variant",
			Description: "To try an idea without touching the game: a variant from its last commit (or a version), in its own folder. open_game its dir to work there; the game stays as it is.",
			Annotations: writes(), InputSchema: object(map[string]any{"name": strArg("e.g. floaty-jump (lowercase, digits, - and _)"),
				"from": strArg("a version from vcs_log (default: the last commit)")}, "name"),
			onGame: func(root string, a toolArgs) toolResult {
				v, err := newVariant(root, a.str("name"), a.str("from"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "variant": v, "next": "open_game its dir to change it; variant_compare to see it beside the game"})
			}},
		{Name: "variant_play", Meta: callableMeta("Starting the variant…", "The variant is open"), Title: "Play a variant",
			Description: "Open a variant in its own window for the person to play (built first if it needs it). " +
				"With at, it opens at that moment of one of their plays of the game (fast-forwarded there with the variant), " +
				"so they try the variant from the spot they marked: \"play the variant from your marker 2\".",
			Annotations: writes(), InputSchema: object(map[string]any{"name": nameArg, "play": playRefArg, "at": strArg(momentArgText + `"start" or "end"; the play's (default: play it from the start)`)}, "name"),
			onGame: func(root string, a toolArgs) toolResult {
				v, err := findVariant(root, a.str("name"))
				if err == nil && a.str("at") != "" {
					return resumeIn(a.call(), root, v.Dir, a.str("play"), a.str("at"))
				}
				if err == nil {
					err = playGame(a.call(), v.Dir)
				}
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult("Variant "+v.Name+" is open for the person to play.", false)
			}},
		{Name: "variant_compare", Meta: studio.toolMeta("Comparing…", "Compared"), Title: "Compare a variant",
			Description: "A variant beside the game (or another variant): the files a's commits changed since it split from b (a scene's per entity) and a frame of each at the same moment, " +
				"both run headless with GL on the person's latest play's inputs to its last marker (their moment, so the change shows where they meant). With sandbox: true, a is a sandbox: the files it differs in from the game now. " +
				"It shows the person both frames in Journeyman Studio, with a button to play the variant from that moment.",
			Annotations: writes(), InputSchema: object(map[string]any{"a": strArg("the variant's name (or the sandbox's)"), "sandbox": boolArg("a is a sandbox, compared with the game"), "b": strArg(`another variant, or "game" (default)`),
				"play": strArg(`replay this recorded play's inputs in both (default: the person's latest; "none": no input)`),
				"at":   strArg(`the moment to show both at: of the play, a marker ("m2"), time or frame (default: its last marker, else its end); with no play, a frame (default 120)`)}, "a"),
			onGame: func(root string, a toolArgs) toolResult {
				b := a.str("b")
				if b == "" {
					b = "game"
				}
				cmp, err := compareVariants(a.call(), root, a.str("a"), b, a.str("play"), a.str("at"), a.flag("sandbox"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				// The Studio around it (its Variants tab shows the comparison), and the comparison itself.
				result := s.listGames(gamesHome())
				result.Text = cmp.text()
				compared := map[string]any{"game": root, "a": cmp.A, "b": cmp.B, "play": cmp.Play, "at": cmp.At, "frame": cmp.Frame, "changes": cmp.Changes, "errors": cmp.Errors, "sandbox": cmp.Sandbox, "uncommitted": cmp.Uncommitted}
				result.Structured.(map[string]any)["compared"] = compared
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
				result.Meta["jm/frames"] = frames
				return result
			}},
		{Name: "variant_merge", Meta: callableMeta("Merging…", "Merged"), Title: "Merge a variant",
			Description: "Bring a variant into the game once the person likes it: its uncommitted work is committed, then merged as a commit. A conflict merges nothing and names the files.",
			Annotations: writes(), InputSchema: object(map[string]any{"name": nameArg}, "name"),
			onGame: func(root string, a toolArgs) toolResult {
				v, err := mergeVariant(root, a.str("name"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "merged": v, "next": "variant_drop it if it's done"})
			}},
		{Name: "variant_drop", Meta: callableMeta("Dropping…", "Dropped"), Title: "Drop a variant",
			Description: "Remove a variant's folder (its uncommitted work with it). Its branch is kept unless it was merged.",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": false},
			InputSchema: object(map[string]any{"name": nameArg}, "name"),
			onGame: func(root string, a toolArgs) toolResult {
				kept, err := dropVariant(root, a.str("name"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult(dropText(a.str("name"), kept), false)
			}},
	}
}
