package main

import (
	"fmt"
	"strings"
	"time"
)

// Sandboxes (jm sandbox) as tools: an experiment of the agent's own runs in a
// throwaway copy, never in the game the person is working on.
func (s *mcpServer) sandboxTools() []mcpTool {
	home := gamesHome() // now: the working folder moves with the open game
	nameArg := strArg("the sandbox's name (sandboxes lists them)")
	return []mcpTool{
		{Name: "sandboxes", Meta: callableMeta("", ""), Title: "The game's sandboxes", Description: "The open game's sandboxes (jm sandbox): throwaway copies for experiments, " +
			"each with the files that differ from the game and when it expires. Expired ones are removed as they're listed.",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": false}, // drops the expired
			InputSchema: object(map[string]any{"game": gameOfTool}),
			run: inRoot(home, func(root string, _ toolArgs) toolResult {
				list := sandboxesOf(root)
				lines := []string{}
				for _, sb := range list {
					lines = append(lines, fmt.Sprintf("%s: %s (%s)", sb.Name, sb.Dir, sb.summary()))
				}
				if len(lines) == 0 {
					lines = append(lines, "No sandboxes: sandbox_new makes one.")
				}
				return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"sandboxes": list}}
			})},
		{Name: "sandbox_new", Meta: callableMeta("Making a sandbox…", "The sandbox is made"), Title: "Make a sandbox",
			Description: "Before an experiment you may throw away (a risky refactor, a tuning sweep, trying a library): copy the game as it is now into a sandbox. " +
				"Change its files (open_game its dir, or sandbox_run), never the game's; promote it to a variant if it works. It expires on its own.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "name": strArg("e.g. gravity-sweep (default: sb1, sb2…)"),
				"hours": intArg("how long to keep it (default 24)")}),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				sb, err := newSandbox(root, a.str("name"), time.Duration(a.num("hours", 24, 1, 24*30))*time.Hour)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "sandbox": sb, "next": "open_game its dir to work there, or sandbox_run"})
			})},
		{Name: "sandbox_run", Meta: callableMeta("Running in the sandbox…", "Done"), Title: "Run jm in a sandbox",
			Description: "Run a jm command in a sandbox, stopped at the timeout: e.g. [\"build\", \"--json\"], [\"test\", \"--json\"], [\"golden\", \"--json\"].",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "name": nameArg, "args": listArg("jm's arguments"),
				"timeout": intArg("seconds before it's stopped (default 300)")}, "name", "args"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				out, err := runInSandbox(root, a.str("name"), a.list("args"), time.Duration(a.num("timeout", 300, 1, 3600))*time.Second)
				if err != nil {
					return textResult(strings.TrimSpace(out+"\n"+err.Error()), true)
				}
				return textResult(out, false)
			})},
		{Name: "sandbox_promote", Meta: callableMeta("Keeping it…", "It's a variant"), Title: "Keep a sandbox as a variant",
			Description: "A sandbox that worked becomes a variant (its changes committed on the variant's branch) for the person to play, compare and merge; the sandbox goes.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "name": nameArg, "variant": strArg("the variant's name")}, "name", "variant"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				v, err := promoteSandbox(root, a.str("name"), a.str("variant"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "variant": v, "next": "variant_compare it with the game"})
			})},
		{Name: "sandbox_drop", Meta: callableMeta("Dropping…", "Dropped"), Title: "Drop a sandbox",
			Description: "Remove a sandbox now (they expire on their own too).",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": false},
			InputSchema: object(map[string]any{"game": gameOfTool, "name": nameArg}, "name"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				if err := dropSandbox(root, a.str("name")); err != nil {
					return textResult(err.Error(), true)
				}
				return textResult("Dropped sandbox "+a.str("name")+".", false)
			})},
	}
}
