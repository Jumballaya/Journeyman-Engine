package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// @-mentions: the person names a part of their game in the composer (ChatGPT
// desktop) and the agent gets it as a resource link. Files are resources
// already; plays, markers (moments: markers with picks) and entities are read
// here (jm://play/…, jm://entity?…).

const mentionTool = "mention_search"

// mentionLimit keeps the picker's list short: typing narrows it.
const mentionLimit = 30

// A mention's link is as a game's files write it (a task's); its resource_link names the game too.
type mention struct {
	kind, name, link, mime, where string
}

func (m mention) resourceLink(root string) map[string]any {
	return map[string]any{"type": "resource_link", "uri": gameURI(root, m.link), "name": m.name, "title": m.name,
		"description": m.kind + " · " + m.where, "mimeType": m.mime}
}

func (s *mcpServer) mentionTools() []mcpTool {
	return []mcpTool{{Name: mentionTool, Title: "Find a part of the game",
		Description: "What the person can @-mention: scenes, prefabs, scripts, design notes, entities, recorded plays and their markers, matching query (empty: the latest first).",
		Annotations: readOnly(), InputSchema: object(map[string]any{"query": strArg("what was typed after @ (may be empty)")}, "query"),
		Meta: map[string]any{"ui": map[string]any{"visibility": []string{"app"}}},
		onGame: func(root string, a toolArgs) toolResult {
			found := searchMentions(mentionables(root), a.str("query"), mentionLimit)
			items := []map[string]any{}
			var lines []string
			for _, m := range found {
				items = append(items, m.resourceLink(root))
				lines = append(lines, m.name+" ("+m.kind+"): "+m.link) // the link the model writes in a task
			}
			return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"items": items}}
		}}}
}

// mentionables are the game's parts in the order an empty query shows them:
// the person's latest play and its markers, scenes, prefabs, scripts,
// entities, then older plays and an agent's runs.
func mentionables(root string) []mention {
	all, _ := plays.List(root)
	var latest, older []mention
	for _, p := range all {
		started := strings.Replace(p.Meta.Started, "T", " ", 1)
		about := fmt.Sprintf("%s, %s long, %d markers", started[:min(len(started), 16)], clock(p.Meta.Seconds), len(p.Meta.Markers))
		if p.ByAgent() {
			about = "an agent's run: " + about
		}
		ms := append([]mention{{"play", "play " + p.ID, playLink(p.ID, 0), "text/plain", about}}, markerMentions(p)...)
		if latest == nil && !p.ByAgent() {
			latest = ms
		} else {
			older = append(older, ms...)
		}
	}
	files, entities := fileMentions(root)
	return append(append(append(latest, files...), entities...), older...)
}

// markerMentions are a play's markers; one with picks is a moment, and says what was picked.
func markerMentions(p *plays.Play) []mention {
	var out []mention
	for _, m := range p.Meta.Markers {
		kind, where := "marker", fmt.Sprintf("play %s at %s, %s", p.ID, clock(m.Time), m.Scene)
		if len(m.Picks) > 0 {
			kind, where = "moment", where+", picked "+strings.Join(pickNames(m), ", ")
		}
		name := fmt.Sprintf("%s %d", kind, m.N)
		if m.Note != "" {
			name += ": " + m.Note
		}
		out = append(out, mention{kind, name, playLink(p.ID, m.N), "text/plain", where})
	}
	return out
}

// fileMentions are the game's scenes, prefabs and scripts, and each
// scene's entities by name (one mention for all of a name in a scene).
func fileMentions(root string) (files, entities []mention) {
	var scenes, prefabs, scripts, notes []mention
	eachGameFile(root, func(rel, real string) {
		// A link is the kind it's named and the kind it leads to (what readers judge), or none.
		is := func(suffix string) bool { return strings.HasSuffix(rel, suffix) && strings.HasSuffix(real, suffix) }
		uri := fileURI(filepath.Join(root, filepath.FromSlash(rel)))
		base := path.Base(rel)
		switch {
		case is(".scene.json"):
			scenes = append(scenes, mention{"scene", strings.TrimSuffix(base, ".scene.json"), uri, "application/json", rel})
			entities = append(entities, entityMentions(rel, real)...)
		case is(".prefab.json"):
			prefabs = append(prefabs, mention{"prefab", strings.TrimSuffix(base, ".prefab.json"), uri, "application/json", rel})
		case is(".ts") && !strings.HasSuffix(rel, ".d.ts") && !strings.HasSuffix(real, ".d.ts"):
			scripts = append(scripts, mention{"script", strings.TrimSuffix(base, ".ts"), uri, "text/x-typescript", rel})
		}
	})
	// The notes as memory reads them (design/ may be a link to a folder in the game), those agents may read.
	for _, n := range memory.Notes(root) {
		abs := filepath.Join(root, filepath.FromSlash(n.Path))
		if _, _, err := gamePath(root, abs); err == nil {
			notes = append(notes, mention{"note", strings.TrimSuffix(filepath.Base(n.Path), ".md"), fileURI(abs), "text/markdown", n.Path})
		}
	}
	return append(append(append(scenes, prefabs...), scripts...), notes...), entities
}

type sceneFile struct {
	Entities []map[string]any `json:"entities"`
}

// eachEntity calls fn with every entity in entities and their children, and
// where it is ("entities[2].children[0]").
func eachEntity(entities []map[string]any, where string, fn func(e map[string]any, where string)) {
	for i, e := range entities {
		at := fmt.Sprintf("%s[%d]", where, i)
		fn(e, at)
		eachEntity(childrenOf(e), at+".children", fn)
	}
}

// childrenOf is an entity's children (none when it has none, or they aren't entities).
func childrenOf(e map[string]any) []map[string]any {
	var children []map[string]any
	if data, err := json.Marshal(e["children"]); err == nil {
		_ = json.Unmarshal(data, &children)
	}
	return children
}

// entityMentions are the named entities of the scene rel (its real file: real).
func entityMentions(rel, real string) []mention {
	var out []mention
	counts := map[string]int{}
	var names []string
	eachEntity(readScene(real).Entities, "entities", func(e map[string]any, _ string) {
		name, _ := e["name"].(string)
		if name == "" {
			return
		}
		if counts[name] == 0 {
			names = append(names, name)
		}
		counts[name]++
	})
	short := strings.TrimSuffix(path.Base(rel), ".scene.json")
	for _, name := range names {
		where := "in " + short
		if counts[name] > 1 {
			where = fmt.Sprintf("%d in %s", counts[name], short)
		}
		out = append(out, mention{"entity", name, entityLink(rel, name), "application/json", where})
	}
	return out
}

func readScene(path string) sceneFile {
	var s sceneFile
	if data, err := os.ReadFile(path); err == nil {
		_ = json.Unmarshal(data, &s)
	}
	return s
}

// searchMentions is what matches query, best first, at most limit: names
// starting with it, then names holding it, then the rest of what's said of them.
func searchMentions(all []mention, query string, limit int) []mention {
	q := strings.ToLower(strings.TrimSpace(query))
	rank := func(m mention) int {
		name := strings.ToLower(m.name)
		switch {
		case q == "" || strings.HasPrefix(name, q):
			return 0
		case strings.Contains(name, q):
			return 1
		case strings.Contains(strings.ToLower(m.kind+" "+m.where), q):
			return 2
		}
		return -1
	}
	var found []mention
	for _, m := range all {
		if rank(m) >= 0 {
			found = append(found, m)
		}
	}
	sort.SliceStable(found, func(i, j int) bool { return rank(found[i]) < rank(found[j]) })
	return found[:min(len(found), limit)]
}
