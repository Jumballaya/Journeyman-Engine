package main

import (
	"context"
	"fmt"
	"path/filepath"
	"strings"
	"time"
)

// The bridge to the editor on the person's screen: what they have open and
// selected, the things they point out to their agent there (Edit > Point Out
// to Your Agent), and the agent showing them things in it.
func (s *mcpServer) editorTools() []mcpTool {
	home := gamesHome()
	return []mcpTool{
		{Name: "editor_state", Title: "What the person has open in the editor",
			Description: "The editor running on the open game: the scene the person has open, what they selected, files with unsaved edits, " +
				"and the notes they pointed out to you there (Edit > Point Out to Your Agent: a note about the selection). " +
				"\"This\" and \"here\" usually mean their selection.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool}), Meta: callableMeta("", ""),
			run: inRoot(home, func(root string, _ toolArgs) toolResult {
				live := liveEditors(root)
				if live == nil {
					live = []editorSession{}
				}
				return toolResult{Text: describeEditors(live), Structured: map[string]any{"editors": live}}
			})},
		{Name: "editor_show", Title: "Show the person something in the editor",
			Description: "Show the person something in the editor on their screen: open a scene, select and frame an entity (by name), with a note they see. " +
				"Opens the editor first when it isn't running. It changes no file.",
			Annotations: writes(), Meta: callableMeta("Showing it in the editor…", "Shown in the editor"),
			InputSchema: object(map[string]any{"game": gameOfTool, "scene": strArg("a scene of the open game, e.g. scenes/main.scene.json (default: the one open there)"),
				"entity": strArg("an entity's name in that scene: it's selected and framed"), "note": strArg("what to tell the person about it, one line")}),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				scene, entity, note := a.str("scene"), a.str("entity"), a.str("note")
				if scene == "" && entity == "" && note == "" {
					return textResult("Say what to show: scene, entity or note.", true)
				}
				var err error
				if scene != "" {
					var path string
					if path, scene, err = gamePath(root, scene); err == nil && !exists(path) {
						err = fmt.Errorf("%s: no such scene in the game", a.str("scene"))
					}
				}
				var editor editorSession
				if err == nil {
					editor, err = runningEditor(a.call().context(), root)
				}
				if err == nil {
					err = editor.show(filepath.ToSlash(scene), entity, note)
				}
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult("Shown in the editor. What the person says about it comes to you; editor_state has their selection.", false)
			})},
	}
}

// runningEditor is the editor on root, opened (and waited for) when none runs.
func runningEditor(ctx context.Context, root string) (editorSession, error) {
	if live := liveEditors(root); len(live) > 0 {
		return live[0], nil
	}
	program, err := findEditor()
	if err == nil {
		err = startEditor(program, root)
	}
	if err != nil {
		return editorSession{}, err
	}
	for deadline := time.Now().Add(30 * time.Second); time.Now().Before(deadline) && ctx.Err() == nil; time.Sleep(250 * time.Millisecond) {
		if live := liveEditors(root); len(live) > 0 {
			return live[0], nil
		}
	}
	return editorSession{}, fmt.Errorf("the editor didn't open on %s within 30 s", root)
}

// describeEditors says what the editors have open, for the model.
func describeEditors(live []editorSession) string {
	if len(live) == 0 {
		return "The editor isn't open on this game (editor_show or project_open_editor opens it)."
	}
	var b strings.Builder
	for _, e := range live {
		switch {
		case e.Scene == "":
			b.WriteString("The editor has no scene open.")
		case len(e.Selection) == 0:
			fmt.Fprintf(&b, "The editor has %s open, nothing selected.", e.Scene)
		default:
			fmt.Fprintf(&b, "The editor has %s open, %s selected.", e.Scene, strings.Join(e.Selection, ", "))
		}
		if len(e.Unsaved) > 0 {
			fmt.Fprintf(&b, " Unsaved: %s.", strings.Join(e.Unsaved, ", "))
		}
		for _, n := range e.Notes {
			what := n.Scene
			if len(n.Entities) > 0 {
				what = strings.Join(n.Entities, ", ") + " in " + n.Scene
			}
			fmt.Fprintf(&b, "\nNote %d (%s, about %s): %s", n.ID, time.Unix(n.At, 0).Format("15:04"), what, n.Text)
		}
		b.WriteString("\n")
	}
	return strings.TrimSpace(b.String())
}
