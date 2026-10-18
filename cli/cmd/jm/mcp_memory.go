package main

import (
	"encoding/json"
	"fmt"
	"path"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
)

// The game's memory (design/) as tools: what the agent reads before a change
// and adds to after one, so the next conversation knows why the game is so.
func (s *mcpServer) memoryTools() []mcpTool {
	home := gamesHome() // now: the working folder moves with the open game
	noteArg := strArg(`a note's title, e.g. "Pillars" (memory lists them)`)
	return []mcpTool{
		{Name: "memory", Meta: callableMeta("", ""), Title: "The game's design notes", Description: "Read before you change how the game plays: the game's design notes (design/: Pitch, Pillars, Levels, Systems, Decisions, Notes), " +
			"or one note with what it links and what links it. [[Note]] links a note; game files are named in backticks.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool, "note": strArg(`one note to read, e.g. "Pillars" (default: the list)`),
				"kind": strArg(`only the dossiers of a kind, e.g. "character", with their properties`)}),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				if a.str("note") != "" {
					n, ok := memory.Find(root, a.str("note"))
					if !ok {
						return textResult(fmt.Sprintf("no note %q: memory lists them", a.str("note")), true)
					}
					v := noteView(root, n)
					return toolResult{Text: n.Text + "\nLinked from: " + strings.Join(v["backlinks"].([]string), ", "), Structured: v}
				}
				notes, kinds := memory.Notes(root), memory.Kinds(root)
				if kind := a.str("kind"); kind != "" {
					var of []memory.Note
					var lines []string
					for _, n := range notes {
						if strings.EqualFold(n.Type, kind) || strings.EqualFold(path.Base(path.Dir(n.Path)), kind) {
							of = append(of, n)
							props, _ := json.Marshal(n.Props)
							lines = append(lines, fmt.Sprintf("%s (%s): %s", n.Title, n.Path, props))
						}
					}
					if len(of) == 0 {
						return textResult(fmt.Sprintf("No %s dossiers yet: dossier_new makes one.", kind), false)
					}
					return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"notes": of}}
				}
				var lines []string
				if len(notes) == 0 { // its kinds still listed: a dossier may be its first note
					lines = append(lines, "No design notes yet: memory_note or memory_decide makes them (or jm memory init).")
				}
				for _, n := range notes {
					lines = append(lines, fmt.Sprintf("%s (%s, %d observations): %s", n.Title, n.Type, len(n.Observations), n.Path))
				}
				lines = append(lines, "", "Dossier kinds (dossier_new):")
				for _, k := range kinds {
					lines = append(lines, fmt.Sprintf("  %s (design/%s/): %s", k.Name, k.Folder, strings.Join(k.Props, ", ")))
				}
				return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"notes": notes, "kinds": kinds}}
			})},
		{Name: "memory_search", Meta: callableMeta("", ""), Title: "Search the design notes", Description: "Find text, in any case, in the design notes' titles and lines (jm memory search).",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool, "text": strArg("what to find")}, "text"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				return jsonResult(map[string]any{"hits": memory.Search(root, a.str("text"))})
			})},
		{Name: "memory_note", Title: "Note it in the design notes", Meta: callableMeta("Noting…", "Noted"),
			Description: "Add an observation to a design note (jm memory note): what the person said about how it feels, a playtest finding, an idea. " +
				"Notes is the default; a note of a new name is made. Link notes with [[Note]].",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "text": strArg("one line"), "note": noteArg,
				"category": strArg("what kind, e.g. feel, idea, bug, playtest, pillar (default note)")}, "text"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				note := a.str("note")
				if note == "" {
					note = "Notes"
				}
				n, err := memory.Observe(root, note, a.str("category"), a.str("text"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "note": n.Title, "path": n.Path})
			})},
		{Name: "dossier_new", Title: "Write a dossier", Meta: callableMeta("Writing…", "Written"),
			Description: "Start a dossier of the game's world: a character, item, location, faction, quest, lore entry, or a kind of the game's own " +
				"(memory lists the kinds and their properties). It's a note in design/<Folder>/ from the kind's template, which Obsidian shows as a table. " +
				`Link what it relates to: props like {"faction": "[[House Dorne]]", "home": "[[Irongate]]"}, and [[links]] in the summary. ` +
				"Read what it relates to first (memory with note) so it agrees with them.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool,
				"kind":    strArg(`e.g. "character", "location" (memory lists the kinds)`),
				"name":    strArg(`its title, e.g. "Mara Dorne"`),
				"summary": strArg("a line or two: who or what it is; [[links]] welcome"),
				"props":   map[string]any{"type": "object", "additionalProperties": map[string]any{"type": "string"}, "description": "its properties, as text"}},
				"kind", "name"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				props := map[string]string{}
				if given, ok := a["props"].(map[string]any); ok {
					for key, v := range given {
						props[key] = fmt.Sprint(v)
					}
				}
				n, err := memory.NewDossier(root, a.str("kind"), a.str("name"), a.str("summary"), props)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: n.Text, Structured: map[string]any{"ok": true, "note": n, "path": n.Path, "obsidian": memory.ObsidianURL(root, n)}}
			})},
		{Name: "memory_decide", Title: "Record a decision", Meta: callableMeta("Recording…", "Recorded"),
			Description: "Record a design decision and why, once you and the person settle one (jm memory decide): its own note in design/decisions, listed in Decisions.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "title": strArg(`what was decided, e.g. "Jumps are variable height"`),
				"why": strArg("the reasons; [[links]] to the notes it serves")}, "title", "why"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				n, err := memory.Decide(root, a.str("title"), a.str("why"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(map[string]any{"ok": true, "note": n.Title, "path": n.Path})
			})},
	}
}

// mcpPrompts are the conversations a person can start from the host's menu.
var mcpPrompts = []struct{ name, title, description, text string }{
	{"new_game_interview", "Plan a new game", "Interview the person about the game they want, then write its design notes.",
		"Interview me about the game I want to make, one short question at a time, waiting for each answer: " +
			"what it is in a sentence; who I play and what I do minute to minute; how it should feel; three pillars every change should serve; " +
			"the first level or area; what's out of scope. Offer a concrete suggestion with each question so I can just say yes. " +
			"As answers come in, write them to the game's design notes with memory_note (note: Pitch, Pillars, Levels, Systems) " +
			"and memory_decide for anything we settle. No game open? new_game first. " +
			"End with a short summary of the notes and the first thing to build."},
}

func promptList() []map[string]any {
	var out []map[string]any
	for _, p := range mcpPrompts {
		out = append(out, map[string]any{"name": p.name, "title": p.title, "description": p.description})
	}
	return out
}

func getPrompt(name string) (map[string]any, bool) {
	for _, p := range mcpPrompts {
		if p.name == name {
			return map[string]any{"description": p.description,
				"messages": []map[string]any{{"role": "user", "content": map[string]any{"type": "text", "text": p.text}}}}, true
		}
	}
	return nil, false
}
