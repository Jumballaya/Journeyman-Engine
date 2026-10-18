package main

import (
	"encoding/json"
	"fmt"
	"path"
	"path/filepath"
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
				if len(notes) == 0 {
					return textResult("No design notes yet: memory_note or memory_decide makes them (or jm memory init).", false)
				}
				var lines []string
				for _, n := range notes {
					lines = append(lines, fmt.Sprintf("%s (%s, %d observations): %s", n.Title, n.Type, len(n.Observations), n.Path))
				}
				lines = append(lines, "", "Dossier kinds (dossier_new):")
				for _, k := range kinds {
					lines = append(lines, fmt.Sprintf("  %s (design/%s/): %s", k.Name, k.Folder, strings.Join(k.Props, ", ")))
				}
				return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"notes": notes, "kinds": kinds}}
			})},
		{Name: "memory_search", Meta: callableMeta("", ""), Title: "Search the design notes", Description: "Search the design notes and dossiers, best first, a hit per note with its best line (jm memory search). " +
			`Words match in any form ("jumps" finds "jump"); "a phrase" as written; field:value in one field: type:character, title:mara, ` +
			"or a property (faction:dorne, region:ashvale); +word must be there, -word mustn't.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool, "text": strArg(`what to find, e.g. "type:location region:ashvale" or "floaty jump"`)}, "text"),
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
		{Name: "onboard", Title: "Plan the game", Meta: callableMeta("", ""),
			Description: "Plan a new game with the person (jm memory onboard), a step at a time: its pitch, feel, pillars, scope, world, " +
				"characters, art and first level, and how the person likes to work. Gives each step, done or not as the notes say, " +
				"and the next one: ask the person its question (with a concrete suggestion they can say yes to), wait, keep the answer " +
				"as it says, then call onboard again for the next. skip a step the game has no need of (world, characters, you). " +
				"Call it after new_game, and when a game's design notes are thin.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "skip": strArg("a step the game doesn't need: world, characters or you")}),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				if skip := a.str("skip"); skip != "" {
					if _, err := memory.Skip(root, skip); err != nil {
						return textResult(err.Error(), true)
					}
				}
				return onboarding(root)
			})},
		{Name: "remember", Title: "Remember this", Meta: callableMeta("Remembering…", "Remembered"),
			Description: "Keep a fact in mind for every conversation (jm memory remember): the game's canon (\"the Dornes were exiled from Varn\"), " +
				"a rule, a constraint, what was tried and why it failed (scope game, in design/Memory.md); or the person's preference for all their games " +
				"(scope you, ~/.jm/memory.md). One fact a call, in a sentence. The same again is kept once; when its answer lists similar facts, " +
				"forget one that's now wrong, or remember with replaces (its id) to update it. Opening the game reads them back. " +
				"What came up in play goes in memory_note; a settled design question in memory_decide.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "text": strArg("the fact, a sentence"),
				"category": strArg("canon, rule, constraint, preference, style, tried... (default fact)"),
				"scope":    map[string]any{"type": "string", "enum": []string{"game", "you"}, "description": "game (default) or you: the person's, for every game"},
				"replaces": strArg("the id of the fact this updates (it keeps the id)")}, "text"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				f, similar, err := factsOf(root, a.str("scope")).Remember(a.str("category"), a.str("text"), a.str("replaces"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				text := fmt.Sprintf("Remembered %s: [%s] %s", f.ID, f.Category, f.Text)
				for _, s := range similar {
					text += fmt.Sprintf("\nSimilar, kept already: %s [%s] %s", s.ID, s.Category, s.Text)
				}
				if similar == nil {
					similar = []memory.Fact{}
				}
				return toolResult{Text: text, Structured: map[string]any{"ok": true, "fact": f, "similar": similar}}
			})},
		{Name: "recall", Meta: callableMeta("", ""), Title: "What's remembered",
			Description: "The facts kept in mind (jm memory recall), the person's and the game's: those a query is about, best first, or all of them.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"game": gameOfTool, "query": strArg(`e.g. "Dorne exile" (default: all)`)}),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				facts := memory.Recall(a.str("query"), factsOf(root, "you"), factsOf(root, "game"))
				if len(facts) == 0 {
					return toolResult{Text: "Nothing remembered yet.", Structured: map[string]any{"facts": []memory.Fact{}}}
				}
				return toolResult{Text: factLines(facts), Structured: map[string]any{"facts": facts}}
			})},
		{Name: "forget", Title: "Forget a fact", Meta: callableMeta("Forgetting…", "Forgotten"),
			Description: "Forget a remembered fact that's no longer so (jm memory forget), by its id (recall lists them).",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameOfTool, "id": strArg(`e.g. "f3a9c"`),
				"scope": map[string]any{"type": "string", "enum": []string{"game", "you"}, "description": "where it's kept (default game)"}}, "id"),
			run: inRoot(home, func(root string, a toolArgs) toolResult {
				f, err := factsOf(root, a.str("scope")).Forget(a.str("id"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: fmt.Sprintf("Forgot %s: %s", f.ID, f.Text), Structured: map[string]any{"ok": true, "fact": f}}
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

// factsOf is where a scope's facts are kept: the person's ("you"), or the game's.
func factsOf(root, scope string) memory.Facts {
	if scope == "you" {
		return memory.PersonFacts(filepath.Join(homeDir(), ".jm", "memory.md"))
	}
	return memory.GameFacts(root)
}

func factLines(facts []memory.Fact) string {
	var lines []string
	for _, f := range facts {
		lines = append(lines, fmt.Sprintf("- [%s] %s (%s, %s)", f.Category, f.Text, f.ID, f.Scope))
	}
	return strings.Join(lines, "\n")
}

// keepInMind is what a conversation about the game at root starts knowing:
// the facts remembered, the person's and the game's (the first 40).
func keepInMind(root string) string {
	facts := memory.Recall("", factsOf(root, "you"), factsOf(root, "game"))
	if len(facts) == 0 {
		return ""
	}
	more := ""
	if len(facts) > 40 {
		facts, more = facts[:40], fmt.Sprintf("\n(%d more: recall)", len(facts)-40)
	}
	return "\n\nKeep in mind (remembered; forget or remember with replaces when one changes):\n" + factLines(facts) + more
}

// onboarding is the plan of a new game: its steps, and the next to ask.
func onboarding(root string) toolResult {
	steps := memory.Onboarding(root, factsOf(root, "you"))
	var b strings.Builder
	for _, s := range steps {
		mark := "[ ]"
		if s.Done {
			mark = "[x]"
		}
		fmt.Fprintf(&b, "%s %s\n", mark, s.Name)
	}
	next, ok := memory.NextStep(steps)
	if !ok {
		b.WriteString("\nThe game is planned. Summarize the notes for the person, and the first thing to build.")
		return toolResult{Text: b.String(), Structured: map[string]any{"steps": steps, "done": true}}
	}
	fmt.Fprintf(&b, "\nNext, %s: ask \"%s\" (with a suggestion), then keep the answer: %s.", next.Name, next.Ask, next.Keep)
	if next.Optional {
		b.WriteString(" If the game has no need of it, onboard with skip: " + next.Name + ".")
	}
	return toolResult{Text: b.String(), Structured: map[string]any{"steps": steps, "next": next, "done": false}}
}

// planned says when the game at root has no plan yet (no pitch): onboard
// makes one ("" once it has; a step left later isn't nagged about).
func planned(root string) string {
	if steps := memory.Onboarding(root, factsOf(root, "you")); steps[0].Done {
		return ""
	}
	return "\n\nIts design isn't planned yet: onboard plans it with the person, a question at a time."
}
