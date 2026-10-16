package main

import (
	"fmt"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"
)

// The game's history (jm vcs) as tools: commit after a change that works, so
// the person can always go back.
func (s *mcpServer) vcsTools() []mcpTool {
	home := gamesHome() // now: the working folder moves with the open game
	fromArg := strArg("a version's hash from vcs_log (default: the last commit)")
	return []mcpTool{
		{Name: "vcs_status", Meta: callableMeta("", ""), Title: "What changed", Description: "What changed in the open game since the last commit (jm vcs): each file with git's mark, and a scene's changes per entity " +
			"(added, removed, or which components changed).",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool}),
			run: inRoot(home, func(root string, _ toolArgs) toolResult {
				changes := gameChanges(root)
				return toolResult{Text: changesText(changes), Structured: map[string]any{"changes": changes}}
			})},
		{Name: "vcs_diff", Meta: callableMeta("", ""), Title: "The line diff", Description: "git's line diff of the open game (or one file) since the last commit or a version (jm vcs diff). Files new to git included.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool, "file": strArg("one file (default: the whole game)"), "from": fromArg}),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				text, err := vcs.Diff(root, a.str("from"), a.str("file"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				if text == "" {
					text = "No differences."
				}
				text = clip(text, 60000)
				// The file it's of, as asked: a page can tell a diff is the one it showed the name of.
				return toolResult{Text: text, Structured: map[string]any{"file": a.str("file"), "diff": text}}
			})},
		{Name: "vcs_commit", Title: "Commit", Meta: callableMeta("Committing…", "Committed"),
			Description: "Save every change in the open game as a version (jm vcs commit), after a change that builds and plays right. " +
				"Say what it does for the player, e.g. \"Make jumps variable height\" (none: one naming the files). A game with no history starts one.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "message": strArg("what the change does")}),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				v, err := commitGame(root, a.str("message"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(v)
			})},
		{Name: "vcs_log", Meta: callableMeta("", ""), Title: "The game's versions", Description: "The open game's versions, newest first (jm vcs log): hash, message, who, when, the files each changed.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool, "count": intArg("how many (default 20)")}),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				versions, err := vcs.Log(root, int(a.num("count", 20, 1, 500)))
				if err != nil {
					return textResult(err.Error(), true)
				}
				var lines []string
				thumbs := map[string]string{}
				for _, v := range versions {
					lines = append(lines, fmt.Sprintf("%s %s (%d files)", v.Hash[:8], v.Subject, len(v.Files)))
					if uri := dataURI(versionThumb(root, v.Hash), 320); uri != "" {
						thumbs[v.Hash] = uri
					}
				}
				return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"versions": versions}, Meta: map[string]any{"jm/thumbs": thumbs}}
			})},
		{Name: "vcs_restore", Title: "Go back to a version", Meta: callableMeta("Restoring…", "Restored"),
			Description: "Put files of the open game (\".\": all of it) back as they were at a version (jm vcs restore). Changes they'd lose go to git's stash first, so nothing is lost; say so to the person.",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": false},
			InputSchema: object(map[string]any{"game": gameOfTool, "files": listArg(`files, or ["."] for the whole game`), "from": fromArg}, "files"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				files, err := gamePaths(root, a.list("files"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				backup, err := vcs.Restore(root, a.str("from"), files)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "restored": files, "backup": backup})
			})},
	}
}

// gamePaths are paths that must be the game's (gamePath), from its folder ("." is the whole game).
func gamePaths(root string, paths []string) ([]string, error) {
	out := []string{}
	for _, p := range paths {
		_, rel, err := gamePath(root, p)
		if err != nil {
			return nil, err
		}
		out = append(out, rel)
	}
	return out, nil
}
