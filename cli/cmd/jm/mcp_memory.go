package main

import (
	"fmt"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
)

// The game's memory (design/) as tools: what the agent reads before a change
// and adds to after one, so the next conversation knows why the game is so.
func (s *mcpServer) memoryTools() []mcpTool {
	noteArg := strArg(`a note's title, e.g. "Pillars" (memory lists them)`)
	return []mcpTool{
		{Name: "memory", Meta: callableMeta("", ""), Title: "The game's design notes", Description: "Read before you change how the game plays: the game's design notes (design/: Pitch, Pillars, Levels, Systems, Decisions, Notes), " +
			"or one note with what it links and what links it. [[Note]] links a note; game files are named in backticks.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"note": strArg(`one note to read, e.g. "Pillars" (default: the list)`)}),
			run: inRoot(func(root string, a toolArgs) toolResult {
				if a.str("note") != "" {
					n, ok := memory.Find(root, a.str("note"))
					if !ok {
						return textResult(fmt.Sprintf("no note %q: memory lists them", a.str("note")), true)
					}
					v := noteView(root, n)
					return toolResult{Text: n.Text + "\nLinked from: " + strings.Join(v["backlinks"].([]string), ", "), Structured: v}
				}
				notes := memory.Notes(root)
				if len(notes) == 0 {
					return textResult("No design notes yet: memory_note or memory_decide makes them (or jm memory init).", false)
				}
				var lines []string
				for _, n := range notes {
					lines = append(lines, fmt.Sprintf("%s (%s, %d observations): %s", n.Title, n.Type, len(n.Observations), n.Path))
				}
				return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"notes": notes}}
			})},
		{Name: "memory_search", Meta: callableMeta("", ""), Title: "Search the design notes", Description: "Find text, in any case, in the design notes' titles and lines (jm memory search).",
			Annotations: readOnly(), InputSchema: object(map[string]any{"text": strArg("what to find")}, "text"),
			run: inRoot(func(root string, a toolArgs) toolResult {
				return jsonResult(map[string]any{"hits": memory.Search(root, a.str("text"))})
			})},
		{Name: "memory_note", Title: "Note it in the design notes", Meta: callableMeta("Noting…", "Noted"),
			Description: "Add an observation to a design note (jm memory note): what the person said about how it feels, a playtest finding, an idea. " +
				"Notes is the default; a note of a new name is made. Link notes with [[Note]].",
			Annotations: writes(), InputSchema: object(map[string]any{"text": strArg("one line"), "note": noteArg,
				"category": strArg("what kind, e.g. feel, idea, bug, playtest, pillar (default note)")}, "text"),
			run: inRoot(func(root string, a toolArgs) toolResult {
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
		{Name: "memory_decide", Title: "Record a decision", Meta: callableMeta("Recording…", "Recorded"),
			Description: "Record a design decision and why, once you and the person settle one (jm memory decide): its own note in design/decisions, listed in Decisions.",
			Annotations: writes(), InputSchema: object(map[string]any{"title": strArg(`what was decided, e.g. "Jumps are variable height"`),
				"why": strArg("the reasons; [[links]] to the notes it serves")}, "title", "why"),
			run: inRoot(func(root string, a toolArgs) toolResult {
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
