package main

import (
	"fmt"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
)

// A playbook is a way to grow the game's knowledge with the person, step
// by step: a prompt in the host's menu (its arguments asked there), and the
// same text from the playbook tool where a host has no prompts (ChatGPT).
type playbook struct {
	name, title, description string
	args                     []playArg
	text                     string // {arg} is the argument; {arg?...} is the text when it's given
}

// playArg is a playbook's argument; name is also what completion suggests
// for it (note: a note's title, kind: a kind of dossier, place: a location).
type playArg struct {
	name, description string
	required          bool
}

var playbooks = []playbook{
	{"new_game_interview", "Plan a new game", "Interview the person about the game they want, then write its design notes.", nil,
		"Plan the game I want to make with me. No game open? new_game first. Then call onboard: it gives the next step's question. " +
			"Ask me it, one short question at a time, with a concrete suggestion so I can just say yes; wait for my answer; " +
			"keep it as the step says (memory_note, remember, dossier_new, world_link); call onboard again. " +
			"Skip a step the game doesn't need (onboard with skip). Use memory_decide for anything we settle. " +
			"End with a short summary of the notes and the first thing to build."},
	{"expand_world", "Grow the world", "Add places, peoples, families and lore around a note, connected to what's there.",
		[]playArg{{"note", "what to build around: a place, nation, faction or family", true}, {"idea", "what to add, e.g. \"the second capital\"", false}},
		"Grow the world around [[{note}]]{idea?: {idea}}. First world_context on {note} (steps 2) and recall about it, " +
			"and read the dossiers that matter (memory with note). Propose three to five new things that fit: places, factions, families, " +
			"businesses, characters, lore. Give each one line and how it connects (a typed relation; a road with its distance), tied to what's " +
			"there (kin, trade and rivalries that cross borders) and to the names linked but not written yet. Wait for me to choose. " +
			"For each I choose: dossier_new with props linking its home, region and faction; world_link each tie once; remember new canon. " +
			"Then health (checks world) and settle any contradiction with me. End with world_context on {note} to show what changed."},
	{"new_dossiers", "Make dossiers", "Make a set of characters, items, places... that fit the game and each other.",
		[]playArg{{"kind", "character, item, location, faction, quest, lore, or the game's own", true}, {"count", "how many (default 3)", false},
			{"theme", "what they're for, e.g. \"the smugglers of Ashvale\"", false}},
		"Make {count?{count}}{count!3} {kind} dossiers{theme?, {theme}}. memory with kind {kind} lists the ones there (don't repeat them) " +
			"and the kind's properties; recall for the canon and the art style. Propose them as a short table: name, one line, " +
			"what they link to; make each distinct (role, want, look). After I pick or change them: dossier_new each, " +
			"props linking existing notes, and world_link the ties stated. End with the list and what they connect to."},
	{"level_from_place", "Turn a place into a level", "Write a level brief from a location's dossier, then build the scene.",
		[]playArg{{"place", "the location dossier", true}},
		"Turn [[{place}]] into a level. Read it (memory with note {place}) and world_context on it; recall the art and scope facts; " +
			"read Levels and Pillars. Write a level brief and show it to me: the idea, what it teaches, its beats, " +
			"who and what is there (their dossiers), how it should feel, which pillar it serves. After I agree: memory_note it " +
			"(note Levels, category level, linking [[{place}]]); generate a scene for it and lay it out from the brief (schema lists " +
			"components; scene_view to look); set the dossier's scene property to the scene file (edit its frontmatter). " +
			"Then build, play it yourself (drive_start, drive, drive_frame) and offer project_play."},
	{"story_outline", "Outline the story", "Outline the story's acts and quests from the world's factions, places and characters.",
		[]playArg{{"premise", "the story in a sentence, if there's one already", false}},
		"Outline the story with me{premise?, from this premise: {premise}}. recall the canon; memory with kind faction, " +
			"then character, then quest; world_context on the main conflict. Propose three acts: each one's goal, the places and " +
			"characters it uses, and its turning point. Then the quests that carry them. After I agree: dossier_new (kind quest) each, " +
			"props linking where it happens and who gives it; world_link leads_to from each quest to the next; memory_decide the premise. " +
			"End with the outline as a list of acts and quests."},
	{"canon_check", "Check against canon", "Check a note against the game's facts and the rest of the world, and settle what disagrees.",
		[]playArg{{"note", "the note or dossier to check", true}},
		"Check [[{note}]] against the canon. Read it (memory with note), recall with its name and what it's about, " +
			"world_context on it, and health (checks world). List each contradiction or drift: quote its line and the fact or note it " +
			"disagrees with. Ask me which holds; then fix the note (file_write), update the fact (remember with replaces) or forget it. " +
			"Report what changed; nothing found is a fine answer."},
	{"art_pass", "Draw it", "Brief, draw and wire a dossier's sprite: Aseprite source, sheet, atlas, in the game.",
		[]playArg{{"note", "the dossier: a character, item or place", true}},
		"Make the art for [[{note}]]. Read it (memory with note) and recall the art facts (sprite size, palette, resolution); " +
			"none yet? Ask me, and remember them (category art). Write a short sprite brief: silhouette, its colors from the palette, " +
			"the animations it needs (idle, walk...), and confirm it with me. Draw it in Aseprite (companions lists the Aseprite companion: " +
			"its tools draw in my open Aseprite) as assets/art/<name>.aseprite, a tag per animation; export a sheet with JSON data (tags on) " +
			"beside it and list the .json in the game's atlas. Set the dossier's sprite property to the .aseprite file (edit its frontmatter). " +
			"Then build and look at it in the game (scene_view or drive_frame). The .aseprite file is the source; keep it."},
}

func findPlaybook(name string) (playbook, bool) {
	for _, p := range playbooks {
		if p.name == name {
			return p, true
		}
	}
	return playbook{}, false
}

// fill is a playbook's text with its arguments: {a} the value; {a?text} text
// when a is given, {a!text} when it isn't. A required one missing is an error.
func (p playbook) fill(args map[string]string) (string, error) {
	text := p.text
	for _, a := range p.args {
		v := strings.TrimSpace(args[a.name])
		if v == "" && a.required {
			return "", fmt.Errorf("%s needs %s: %s", p.name, a.name, a.description)
		}
		text = ifGiven(text, "{"+a.name+"?", v != "")
		text = ifGiven(text, "{"+a.name+"!", v == "")
		if rel, ok := strings.CutPrefix(v, memory.Dir+"/"); ok && strings.HasSuffix(rel, ".md") { // a note by its path: linked from the vault, as North/Harbor
			text = strings.ReplaceAll(text, "[[{"+a.name+"}]]", "[["+strings.TrimSuffix(rel, ".md")+"]]")
		}
		text = strings.ReplaceAll(text, "{"+a.name+"}", v)
	}
	return text, nil
}

// ifGiven keeps or drops each "{open...}" in text (a group nests one {arg}).
func ifGiven(text, open string, keep bool) string {
	for {
		start := strings.Index(text, open)
		if start < 0 {
			return text
		}
		depth, end := 0, start+len(open)
		for ; end < len(text) && (text[end] != '}' || depth > 0); end++ {
			if text[end] == '{' {
				depth++
			} else if text[end] == '}' {
				depth--
			}
		}
		inner := ""
		if keep && end <= len(text) {
			inner = text[start+len(open) : min(end, len(text))]
		}
		text = text[:start] + inner + text[min(end+1, len(text)):]
	}
}

func promptList() []map[string]any {
	var out []map[string]any
	for _, p := range playbooks {
		prompt := map[string]any{"name": p.name, "title": p.title, "description": p.description}
		if len(p.args) > 0 {
			var args []map[string]any
			for _, a := range p.args {
				args = append(args, map[string]any{"name": a.name, "description": a.description, "required": a.required})
			}
			prompt["arguments"] = args
		}
		out = append(out, prompt)
	}
	return out
}

func getPrompt(name string, args map[string]string) (map[string]any, *rpcError) {
	p, ok := findPlaybook(name)
	if !ok {
		return nil, &rpcError{Code: -32602, Message: "unknown prompt " + name}
	}
	text, err := p.fill(args)
	if err != nil {
		return nil, &rpcError{Code: -32602, Message: err.Error()}
	}
	return map[string]any{"description": p.description,
		"messages": []map[string]any{{"role": "user", "content": map[string]any{"type": "text", "text": text}}}}, nil
}

// knowledgeValues are what a playbook's note, kind or place can be in the game at root.
func knowledgeValues(root, name string) []string {
	var out []string
	switch name {
	case "kind":
		for _, k := range memory.Kinds(root) {
			out = append(out, k.Name)
		}
	case "note", "place":
		notes, titles := memory.Notes(root), map[string]int{}
		for _, n := range notes {
			titles[strings.ToLower(n.Title)]++
		}
		for _, n := range notes {
			if name == "note" || n.Type == "location" {
				if titles[strings.ToLower(n.Title)] > 1 {
					out = append(out, n.Path) // two notes of one title: each by its path
				} else {
					out = append(out, n.Title)
				}
			}
		}
	}
	sort.Strings(out)
	return out
}

func (s *mcpServer) playbookTools() []mcpTool {
	var names []string
	for _, p := range playbooks {
		names = append(names, p.name)
	}
	return []mcpTool{
		{Name: "playbook", Title: "A playbook", Meta: callableMeta("", ""),
			Description: "Ways to grow the game's knowledge with the person, a step at a time: plan a new game, grow the world around a note, " +
				"make a set of dossiers, turn a place into a level, outline the story, check a note against canon, draw a dossier's art. " +
				"Without a name, lists them and their arguments; with one, gives its steps for the arguments: then do them, " +
				"asking the person where they say. The same are the host's prompts, where it has a menu of them.",
			Annotations: readOnly(), InputSchema: object(map[string]any{
				"name": map[string]any{"type": "string", "enum": names, "description": "the playbook"},
				"args": map[string]any{"type": "object", "additionalProperties": map[string]any{"type": "string"}, "description": `its arguments, e.g. {"note": "Ashvale"}`}}),
			run: func(a toolArgs) toolResult {
				name := a.str("name")
				if name == "" {
					var b strings.Builder
					for _, p := range playbooks {
						fmt.Fprintf(&b, "- %s: %s", p.name, p.description)
						for _, arg := range p.args {
							fmt.Fprintf(&b, " %s%s", arg.name, map[bool]string{true: " (required)", false: ""}[arg.required])
						}
						b.WriteString("\n")
					}
					return toolResult{Text: b.String(), Structured: map[string]any{"playbooks": promptList()}}
				}
				p, ok := findPlaybook(name)
				if !ok {
					return textResult("no playbook "+name+": playbook without a name lists them", true)
				}
				args := map[string]string{}
				if given, ok := a["args"].(map[string]any); ok {
					for k, v := range given {
						args[k] = fmt.Sprint(v)
					}
				}
				text, err := p.fill(args)
				if err != nil {
					return textResult(err.Error()+". Ask the person, then call playbook again.", true)
				}
				return toolResult{Text: text, Structured: map[string]any{"name": p.name, "steps": text}}
			}},
	}
}
