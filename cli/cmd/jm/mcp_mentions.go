package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// @-mentions: the person names a part of their game in the composer (ChatGPT
// desktop) and the agent gets it as a resource link. Files are resources
// already; plays, markers and entities are read here (jm://play/…, jm://entity?…).

const mentionTool = "mention_search"

// mentionLimit keeps the picker's list short: typing narrows it.
const mentionLimit = 30

type mention struct {
	kind, name, uri, mime, where string
}

func (m mention) link() map[string]any {
	return map[string]any{"type": "resource_link", "uri": m.uri, "name": m.name, "title": m.name,
		"description": m.kind + " · " + m.where, "mimeType": m.mime}
}

func (s *mcpServer) mentionTools() []mcpTool {
	return []mcpTool{{Name: mentionTool, Title: "Find a part of the game",
		Description: "What the person can @-mention: scenes, prefabs, scripts, design notes, entities, recorded plays and their markers, matching query (empty: the latest first).",
		Annotations: readOnly(), InputSchema: object(map[string]any{"query": strArg("what was typed after @ (may be empty)")}),
		Meta: map[string]any{"ui": map[string]any{"visibility": []string{"app"}}},
		run: func(a toolArgs) toolResult {
			found := searchMentions(mentionables(), a.str("query"), mentionLimit)
			items := []map[string]any{}
			var lines []string
			for _, m := range found {
				items = append(items, m.link())
				lines = append(lines, m.name+" ("+m.kind+"): "+m.uri)
			}
			return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"items": items}}
		}}}
}

// mentionables are the open game's parts in the order an empty query shows
// them: the latest play and its markers, scenes, prefabs, scripts, entities, older plays.
func mentionables() []mention {
	root, err := projectRoot()
	if err != nil {
		return nil
	}
	all, _ := plays.List(root)
	var latest, older []mention
	for i, p := range all {
		started := strings.Replace(p.Meta.Started, "T", " ", 1)
		ms := append([]mention{{"play", "play " + p.ID, "jm://play/" + url.PathEscape(p.ID), "text/plain",
			fmt.Sprintf("%s, %s long, %d markers", started[:min(len(started), 16)], clock(p.Meta.Seconds), len(p.Meta.Markers))}},
			markerMentions(p)...)
		if i == 0 {
			latest = ms
		} else {
			older = append(older, ms...)
		}
	}
	files, entities := fileMentions()
	return append(append(append(latest, files...), entities...), older...)
}

func markerMentions(p *plays.Play) []mention {
	var out []mention
	for _, m := range p.Meta.Markers {
		name := fmt.Sprintf("marker %d", m.N)
		if m.Note != "" {
			name += ": " + m.Note
		}
		out = append(out, mention{"marker", name, fmt.Sprintf("jm://play/%s/m%d", url.PathEscape(p.ID), m.N), "text/plain",
			fmt.Sprintf("play %s at %s, %s", p.ID, clock(m.Time), m.Scene)})
	}
	return out
}

// fileMentions are the project's scenes, prefabs and scripts, and each
// scene's entities by name (one mention for all of a name in a scene).
func fileMentions() (files, entities []mention) {
	var scenes, prefabs, scripts, notes []mention
	_ = filepath.WalkDir(".", func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() {
			if path != "." && hiddenFromAgents(path) {
				return filepath.SkipDir
			}
			return nil
		}
		real, err := agentFile(path)
		if err != nil {
			return nil // a link out of the project, or into build/
		}
		rel := filepath.ToSlash(path)
		// A link is the kind it's named and the kind it leads to (what readers judge), or none.
		real = filepath.ToSlash(real)
		is := func(suffix string) bool { return strings.HasSuffix(rel, suffix) && strings.HasSuffix(real, suffix) }
		abs, _ := filepath.Abs(path)
		uri := fileURI(abs)
		base := filepath.Base(rel)
		switch {
		case is(".scene.json"):
			scenes = append(scenes, mention{"scene", strings.TrimSuffix(base, ".scene.json"), uri, "application/json", rel})
			entities = append(entities, entityMentions(rel)...)
		case is(".prefab.json"):
			prefabs = append(prefabs, mention{"prefab", strings.TrimSuffix(base, ".prefab.json"), uri, "application/json", rel})
		case is(".md") && strings.HasPrefix(rel, memory.Dir+"/"):
			notes = append(notes, mention{"note", strings.TrimSuffix(base, ".md"), uri, "text/markdown", rel})
		case is(".ts") && !strings.HasSuffix(rel, ".d.ts") && !strings.HasSuffix(real, ".d.ts"):
			scripts = append(scripts, mention{"script", strings.TrimSuffix(base, ".ts"), uri, "text/x-typescript", rel})
		}
		return nil
	})
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

func entityMentions(scene string) []mention {
	var out []mention
	counts := map[string]int{}
	var names []string
	eachEntity(readScene(scene).Entities, "entities", func(e map[string]any, _ string) {
		name, _ := e["name"].(string)
		if name == "" {
			return
		}
		if counts[name] == 0 {
			names = append(names, name)
		}
		counts[name]++
	})
	short := strings.TrimSuffix(filepath.Base(scene), ".scene.json")
	for _, name := range names {
		where := "in " + short
		if counts[name] > 1 {
			where = fmt.Sprintf("%d in %s", counts[name], short)
		}
		out = append(out, mention{"entity", name, entityURI(scene, name), "application/json", where})
	}
	return out
}

func entityURI(scene, name string) string {
	return "jm://entity?" + url.Values{"scene": {scene}, "name": {name}}.Encode()
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

// readMentioned reads a play, marker or entity a mention linked; false: uri
// isn't one of those.
func readMentioned(uri string) ([]map[string]any, bool, error) {
	if rest, ok := strings.CutPrefix(uri, "jm://play/"); ok {
		contents, err := readPlayMention(uri, rest)
		return contents, true, err
	}
	if strings.HasPrefix(uri, "jm://entity?") {
		contents, err := readEntityMention(uri)
		return contents, true, err
	}
	return nil, false, nil
}

func readPlayMention(uri, rest string) ([]map[string]any, error) {
	id, marker, _ := strings.Cut(rest, "/")
	id, err := url.PathUnescape(id)
	if err != nil {
		return nil, err
	}
	_, p, b, err := openPlay(id)
	if err != nil {
		return nil, err
	}
	if p.ID != id {
		return nil, fmt.Errorf("no play %s", id) // a prefix finds a play: a link names one
	}
	o, err := overviewOf(p, b)
	if err != nil {
		return nil, err
	}
	if marker == "" {
		return []map[string]any{{"uri": uri, "mimeType": "text/plain", "text": o.text()}}, nil
	}
	for _, m := range o.Markers {
		if fmt.Sprintf("m%d", m.N) != marker {
			continue
		}
		text := fmt.Sprintf("Marker %d of play %s at %s (frame %d), %s", m.N, p.ID, clock(m.Time), m.Frame, m.Scene)
		if m.Note != "" {
			text += fmt.Sprintf(": %q", m.Note)
		}
		if len(m.Pressed) > 0 {
			text += "\npressed just before: " + pressesText(m.Pressed)
		}
		text += fmt.Sprintf("\nplay_frame / play_state with play %q and at %q show it.", p.ID, marker)
		contents := []map[string]any{{"uri": uri, "mimeType": "text/plain", "text": text}}
		if data := jpegOf(markerImage(p, m), 960); data != nil {
			contents = append(contents, map[string]any{"uri": uri, "mimeType": "image/jpeg", "blob": base64.StdEncoding.EncodeToString(data)})
		}
		return contents, nil
	}
	return nil, fmt.Errorf("play %s has no marker %s", p.ID, marker)
}

func readEntityMention(uri string) ([]map[string]any, error) {
	u, err := url.Parse(uri)
	if err != nil {
		return nil, err
	}
	scene, name := u.Query().Get("scene"), u.Query().Get("name")
	rel, err := agentFile(scene)
	if err != nil {
		return nil, err
	}
	if !strings.HasSuffix(rel, ".scene.json") {
		return nil, fmt.Errorf("%s isn't one of the project's scenes", scene)
	}
	type found struct {
		Where  string         `json:"where"`
		Entity map[string]any `json:"entity"`
	}
	var matches []found
	eachEntity(readScene(rel).Entities, "entities", func(e map[string]any, where string) {
		if e["name"] == name {
			matches = append(matches, found{where, e})
		}
	})
	if len(matches) == 0 {
		return nil, fmt.Errorf("no entity %q in %s", name, scene)
	}
	data, _ := json.MarshalIndent(map[string]any{"scene": filepath.ToSlash(rel), "name": name, "entities": matches}, "", "  ")
	return []map[string]any{{"uri": uri, "mimeType": "application/json", "text": string(data)}}, nil
}
