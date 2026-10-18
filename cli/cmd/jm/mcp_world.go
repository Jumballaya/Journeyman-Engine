package main

import (
	"fmt"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
)

// The game's world as a web: what's around a note, how two connect, and a
// typed relation added. It's the design notes' links: dossiers' properties
// ([[links]]), their relations ("- located_in [[Ashvale]]") and mentions.
func (s *mcpServer) worldTools() []mcpTool {
	home := gamesHome()
	return []mcpTool{
		{Name: "world_context", Meta: callableMeta("", ""), Title: "What's around this in the world",
			Description: "Before writing a new place, person or thing, or changing one: the note and what's connected to it within steps " +
				"(default 2): each note's kind and summary, the connections (relations with their details, properties naming notes, mentions) " +
				"and the names linked that have no note yet. E.g. making a second capital: world_context on its region or its nation.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool, "note": strArg(`a note's title, e.g. "Ashvale"`),
				"steps": map[string]any{"type": "integer", "minimum": 1, "maximum": 4, "description": "how far out (default 2)"}}, "note"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				w := memory.WorldOf(root)
				center, dist, edges, err := w.Around(a.str("note"), int(a.num("steps", 2, 1, 4)))
				if err != nil {
					return textResult(err.Error()+": memory lists the notes", true)
				}
				return worldView(w, center, dist, edges)
			})},
		{Name: "world_path", Meta: callableMeta("", ""), Title: "How two things connect",
			Description: "The shortest chain of connections between two notes (relations and properties first, then mentions), e.g. how Mara Dorne is tied to Ost.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool, "from": strArg("a note's title"), "to": strArg("another")}, "from", "to"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				chain, err := memory.WorldOf(root).Path(a.str("from"), a.str("to"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				if chain == nil {
					return toolResult{Text: fmt.Sprintf("%s and %s don't connect yet.", a.str("from"), a.str("to")), Structured: map[string]any{"path": []memory.Edge{}}}
				}
				var lines []string
				for _, e := range chain {
					lines = append(lines, edgeLine(e))
				}
				return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"path": chain}}
			})},
		{Name: "world_link", Title: "Connect two things", Meta: callableMeta("Connecting…", "Connected"),
			Description: "State a typed relation in a note: \"- type [[target]] (details)\" in its Relations, e.g. Irongate road_to Duskhold {distance: \"120 km\"}. " +
				"State it once: the other side reads it (located_in/contains, owns/owned_by, ...; road_to, borders, rival_of, at_war_with read the same both ways; " +
				"a note named Relations adds \"- [inverse] a b\" or \"- [symmetric] a\"). The target may not have a note yet.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "from": strArg("the note that states it"),
				"type": strArg("one word: located_in, capital_of, member_of, road_to, ..."), "to": strArg("what it relates to"),
				"details": map[string]any{"type": "object", "additionalProperties": map[string]any{"type": "string"}, "description": `e.g. {"distance": "120 km", "via": "[[Black Pass]]"}`}},
				"from", "type", "to"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				details := map[string]string{}
				if given, ok := a["details"].(map[string]any); ok {
					for k, v := range given {
						details[k] = fmt.Sprint(v)
					}
				}
				n, err := memory.Relate(root, a.str("from"), memory.Relation{Type: a.str("type"), Target: a.str("to"), Attrs: details})
				if err != nil {
					return textResult(err.Error(), true)
				}
				text := "Stated in " + n.Path
				if conflicts := memory.WorldOf(root).Conflicts(); len(conflicts) > 0 {
					text += "\nThe world now disagrees with itself:"
					for _, c := range conflicts {
						text += "\n  " + c.Path + ": " + c.Message
					}
				}
				return toolResult{Text: text, Structured: map[string]any{"ok": true, "note": n.Title, "path": n.Path}}
			})},
	}
}

// worldView is world_context's answer: the note, the notes around it by
// steps, the connections and the unwritten names among them.
func worldView(w memory.World, center memory.Note, dist map[string]int, edges []memory.Edge) toolResult {
	byTitle := map[string]memory.Note{}
	for _, n := range w.Notes {
		byTitle[n.Title] = n
	}
	type near struct {
		Title   string         `json:"title"`
		Type    string         `json:"type,omitempty"`
		Path    string         `json:"path,omitempty"`
		Steps   int            `json:"steps"`
		Summary string         `json:"summary,omitempty"`
		Props   map[string]any `json:"props,omitempty"`
		Missing bool           `json:"missing,omitempty"`
	}
	var notes []near
	for title, steps := range dist {
		n, ok := byTitle[title]
		notes = append(notes, near{title, n.Type, n.Path, steps, memory.Summary(n), n.Props, !ok})
	}
	sort.Slice(notes, func(i, j int) bool {
		if notes[i].Steps != notes[j].Steps {
			return notes[i].Steps < notes[j].Steps
		}
		return notes[i].Title < notes[j].Title
	})
	lines := []string{fmt.Sprintf("%s (%s, %s): %s", center.Title, center.Type, center.Path, memory.Summary(center))}
	if center.Path == "" {
		lines[0] = center.Title + ": linked, no note yet (dossier_new writes one)"
	}
	var unwritten []string
	for _, n := range notes[1:] {
		if n.Missing {
			unwritten = append(unwritten, n.Title)
			continue
		}
		lines = append(lines, fmt.Sprintf("  %d step%s: %s (%s): %s", n.Steps, map[bool]string{true: "", false: "s"}[n.Steps == 1], n.Title, n.Type, n.Summary))
	}
	lines = append(lines, "Connections:")
	for _, e := range edges {
		lines = append(lines, "  "+edgeLine(e))
	}
	if len(unwritten) > 0 {
		lines = append(lines, "Linked, no note yet: "+strings.Join(unwritten, ", "))
	}
	if edges == nil {
		edges = []memory.Edge{}
	}
	return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"note": center, "near": notes, "edges": edges, "unwritten": unwritten}}
}

func edgeLine(e memory.Edge) string {
	line := fmt.Sprintf("%s —%s→ %s", e.From, e.Type, e.To)
	if len(e.Attrs) > 0 {
		var parts []string
		for k, v := range e.Attrs {
			parts = append(parts, k+": "+v)
		}
		sort.Strings(parts)
		line += " (" + strings.Join(parts, ", ") + ")"
	}
	if e.Inverse != "" && e.Inverse != e.Type {
		line += fmt.Sprintf("  [%s %s %s]", e.To, e.Inverse, e.From)
	}
	return line
}
