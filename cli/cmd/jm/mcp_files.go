package main

import (
	"encoding/base64"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/formats"
)

// The open game's files as tools: what a host without a shell (ChatGPT)
// needs to manage them, each the same function jm files uses.
func (s *mcpServer) fileTools() []mcpTool {
	pathArg := strArg("a file of the open game, from its folder, e.g. scenes/main.scene.json")
	return []mcpTool{
		{Name: "files", Meta: callableMeta("", ""), Title: "The game's files", Description: "The open game's files (not .git, .jm, build, dist, node_modules) with sizes and git's mark for each changed one " +
			"(M changed, A added, ? new to git, D deleted, U in conflict). jm files.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}),
			OutputSchema: object(map[string]any{"game": typed("string"), "gameName": typed("string"), "files": typed("array")}, "game", "gameName", "files"),
			onGame: func(root string, _ toolArgs) toolResult {
				list, err := listFiles(root)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: filesText(list), Structured: map[string]any{"game": root, "gameName": gameName(root), "files": list}}
			}},
		{Name: "file_read", Meta: callableMeta("", ""), Title: "Read a file", Description: "A file of the open game: its text, an etag (give it to file_write so you don't save over someone's change) " +
			"and what's wrong with it (JSON that doesn't parse or isn't in jm fmt's layout, a scene's or prefab's problems with the engine's schema). An image comes back as one.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"path": pathArg}, "path"),
			OutputSchema: object(map[string]any{"path": typed("string"), "text": typed("string"), "binary": typed("boolean"), "size": typed("integer"),
				"etag": typed("string"), "problems": typed("array")}, "path", "text", "size", "etag"),
			onGame: func(root string, a toolArgs) toolResult {
				f, err := readGameFile(root, a.str("path"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				if f.Binary && imageFile(f.Path) {
					abs, text := filepath.Join(root, filepath.FromSlash(f.Path)), fmt.Sprintf("%s, %s.", f.Path, sizeText(int64(f.Size)))
					if data, err := os.ReadFile(abs); err == nil && strings.EqualFold(filepath.Ext(f.Path), ".png") && len(data) <= 1<<20 {
						// As it is: pixel art and transparency don't survive a JPEG.
						return toolResult{Text: text, Images: []mcpImage{{data, "image/png"}}, Structured: f,
							Meta: map[string]any{"jm/image": "data:image/png;base64," + base64.StdEncoding.EncodeToString(data)}}
					}
					return imageResult(abs, text, f)
				}
				return jsonResult(f)
			}},
		{Name: "file_write", Meta: callableMeta("", ""), Title: "Write a file", Description: "Save a file of the open game (made with its folders if new). JSON must parse; jm's JSON is saved in jm fmt's layout. " +
			"With etag (from file_read), it's saved only over that version: \"\" means only if there's no file yet. Answers the saved etag and anything wrong with the file.",
			Annotations: idempotent(map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": false}), // replaces the whole file
			OutputSchema: object(map[string]any{"path": typed("string"), "etag": typed("string"), "text": typed("string"), "created": typed("boolean"),
				"problems": typed("array")}, "path", "etag", "text"),
			InputSchema: object(map[string]any{"path": pathArg, "text": strArg("the whole file"),
				"etag": strArg("the version read (file_read); \"\": the file must be new")}, "path", "text"),
			onGame: func(root string, a toolArgs) toolResult {
				path, okPath := a["path"].(string)
				text, okText := a["text"].(string)
				var etag *string
				if v, given := a["etag"]; given {
					s, ok := v.(string)
					okText = okText && ok
					etag = &s
				}
				if !okPath || !okText {
					return textResult("file_write takes path and text, and etag if given, as strings: nothing was saved", true)
				}
				done, err := writeGameFile(root, path, text, etag)
				if errors.Is(err, errChanged) {
					return textResult(a.str("path")+": "+err.Error()+": file_read it again and redo the change", true)
				}
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(done)
			}},
		{Name: "file_move", Meta: callableMeta("", ""), Title: "Rename a file", Description: "Rename or move a file or folder of the open game (folders to it are made). " +
			"Scenes and prefabs that name it aren't changed: search for its old path (files_search) and fix them.",
			Annotations: idempotent(writes()), InputSchema: object(map[string]any{"from": pathArg, "to": strArg("its new path, which mustn't exist yet")}, "from", "to"),
			onGame: func(root string, a toolArgs) toolResult {
				from, to, err := moveGameFile(root, a.str("from"), a.str("to"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "from": from, "to": to})
			}},
		{Name: "file_delete", Meta: callableMeta("", ""), Title: "Delete a file", Description: "Delete a file or folder (and all in it) of the open game. Git can bring back what it tracked.",
			Annotations: idempotent(map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": false}),
			InputSchema: object(map[string]any{"path": pathArg}, "path"),
			onGame: func(root string, a toolArgs) toolResult {
				rel, err := deleteGameFile(root, a.str("path"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "deleted": rel})
			}},
		{Name: "files_search", Meta: callableMeta("", ""), Title: "Search the game", Description: "Find text, in any case, in the open game's file names and lines: the first 200 hits, in path order. jm files search.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"text": strArg("what to find")}, "text"),
			OutputSchema: object(map[string]any{"hits": typed("array"), "truncated": typed("boolean")}, "hits"),
			onGame: func(root string, a toolArgs) toolResult {
				return jsonResult(searchFiles(root, a.str("text"), searchLimit))
			}},
		{Name: "file_open", Title: "Open a file on the desktop", Meta: callableMeta("Opening…", "Opened"),
			Description: "Open a file of the open game on the person's screen: text in their code editor (VS Code, Cursor, Zed, Sublime Text; else the plain text editor), others in their default app.",
			Annotations: writes(), InputSchema: object(map[string]any{"path": pathArg}, "path"),
			onGame: func(root string, a toolArgs) toolResult {
				f, err := readGameFile(root, a.str("path"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				app, err := openOnDesktop(filepath.Join(root, filepath.FromSlash(f.Path)), !f.Binary)
				if err != nil {
					return textResult(fmt.Sprintf("couldn't open %s in %s: %v", f.Path, app, err), true)
				}
				return textResult(fmt.Sprintf("Opened %s in %s.", f.Path, app), false)
			}},
	}
}

// filesText is the list for the model: what changed, then every path while there are few.
func filesText(list []projectEntry) string {
	var changed, all []string
	for _, f := range list {
		all = append(all, f.Path)
		if f.Status != "" {
			changed = append(changed, f.Status+" "+f.Path)
		}
	}
	text := fmt.Sprintf("%d files.", len(list))
	if len(changed) > 0 {
		text += " Changed (git): " + strings.Join(changed, ", ") + "."
	}
	if len(all) <= 300 {
		text += "\n" + strings.Join(all, "\n")
	} else {
		text += " structuredContent lists them all; files_search finds one."
	}
	return text
}

// imageFile says whether path is an image the engine reads (a PNG or JPEG).
func imageFile(path string) bool {
	f, _ := formats.Of(strings.ToLower(path))
	return f.Archive == "image"
}
