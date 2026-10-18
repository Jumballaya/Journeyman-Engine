package memory

import (
	"fmt"
	"slices"
	"strings"
)

// Step is one part of planning a new game with the person: what to ask,
// where the answer goes, and whether the notes have it yet. An optional
// step is skipped when the game has no need of it (a puzzle with no world).
type Step struct {
	Name     string `json:"name"`
	Done     bool   `json:"done"`
	Optional bool   `json:"optional,omitempty"`
	Ask      string `json:"ask"`
	Keep     string `json:"keep"` // where the answer goes: the tools to write it with
}

// Onboarding is planning the game at root, step by step, done or not as
// its notes and facts say; you are the person's facts (asked once, for
// every game).
func Onboarding(root string, you Facts) []Step {
	notes := Notes(root)
	observed := func(title string, categories ...string) bool {
		for _, n := range notes {
			if !strings.EqualFold(n.Title, title) && title != "" {
				continue
			}
			for _, o := range n.Observations {
				if len(categories) == 0 || slices.ContainsFunc(categories, func(c string) bool { return strings.EqualFold(c, o.Category) }) {
					return true
				}
			}
		}
		return false
	}
	kinds := map[string]int{}
	for _, n := range notes {
		if n.Type != "" {
			kinds[n.Type]++
		}
	}
	facts, skipped := map[string]bool{}, map[string]bool{}
	for _, f := range GameFacts(root).All() {
		facts[strings.ToLower(f.Category)] = true
		if strings.EqualFold(f.Category, "skip") {
			skipped[strings.ToLower(f.Text)] = true
		}
	}
	world := kinds["location"]+kinds["faction"]+kinds["lore"] > 0
	steps := []Step{
		{Name: "pitch", Done: observed("Pitch"),
			Ask:  "What is the game, in a sentence? Who do you play, and what do you do minute to minute?",
			Keep: "memory_note (note Pitch; category pitch, then loop for the minute to minute)"},
		{Name: "feel", Done: observed("", "feel"),
			Ask:  "How should it feel to play: two or three words, and a game it should feel like?",
			Keep: "memory_note (note Pitch, category feel)"},
		{Name: "pillars", Done: observed("Pillars"),
			Ask:  "What three things should every change serve?",
			Keep: "memory_note (note Pillars, category pillar), one each"},
		{Name: "scope", Done: facts["scope"],
			Ask:  "What is it not: what's out of scope, and how big is the first version?",
			Keep: "remember (category scope), a fact each"},
		{Name: "world", Done: world || facts["world"], Optional: true,
			Ask: "Is there a world to keep track of: places, peoples, factions, its history? The setting in a sentence, " +
				"then its main places and powers; or say there's none.",
			Keep: "remember (category world) the setting, or that there's no world; dossier_new each place, faction and piece of lore, " +
				"world_link how they connect (located_in, capital_of, at_war_with, road_to with a distance)"},
		{Name: "characters", Done: kinds["character"] > 0, Optional: true,
			Ask:  "Who's in it: the player character, and the two or three others who matter most?",
			Keep: "dossier_new (kind character) each, linked to their home and faction; world_link member_of"},
		{Name: "art", Done: facts["art"] || facts["style"],
			Ask: "How does it look: the sprite size (16x16? 32x32?), the palette, the picture's resolution? " +
				"Art is drawn in Aseprite: its .aseprite files go in assets/art/.",
			Keep: "remember (category art) a fact each: size, palette, resolution, where the .aseprite sources live"},
		{Name: "first level", Done: observed("Levels"),
			Ask:  "What's the first level or area: its idea, what it teaches, how it should feel?",
			Keep: "memory_note (note Levels, category level); for a world, link the level to its location dossier"},
		{Name: "you", Done: len(you.All()) > 0, Optional: true,
			Ask: "Asked once, for all your games: how do you like to work? (e.g. small changes, play after each; " +
				"ask before big ones; how you like to be told what changed)",
			Keep: "remember (scope you, category preference)"},
	}
	for i := range steps {
		steps[i].Done = steps[i].Done || steps[i].Optional && skipped[steps[i].Name]
	}
	return steps
}

// Skip is the fact that skips an optional step: the game has no need of it.
func Skip(root, step string) (Fact, error) {
	for _, s := range Onboarding(root, Facts{}) {
		if strings.EqualFold(s.Name, step) {
			if !s.Optional {
				break
			}
			f, _, err := GameFacts(root).Remember("skip", s.Name, "")
			return f, err
		}
	}
	return Fact{}, fmt.Errorf("%q isn't a step to skip (world, characters and you are)", step)
}

// NextStep is the first step not done (ok false: the game is planned).
func NextStep(steps []Step) (Step, bool) {
	for _, s := range steps {
		if !s.Done {
			return s, true
		}
	}
	return Step{}, false
}
