package main

import (
	"bufio"
	"cmp"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/examples"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// The forms jm's tools show the person (mcp_forms.go): making a game and
// pointing at the game's files.

// newGameChoice is what the person picked for a new game.
type newGameChoice struct {
	name, from string         // from: an example, "" for an empty game
	settings   map[string]any // .jm.json paths -> values, for the picture size
}

// pictures are the shapes an empty game can start in: .jm.json's window and logical size.
var pictures = []struct {
	key, title, description string
	window, logical         [2]int
}{
	{"wide", "Wide 1280×720", "An HD window, drawn at its size.", [2]int{1280, 720}, [2]int{1280, 720}},
	{"pixel", "Pixel art 480×270", "Drawn small and scaled up 3× to a 1440×810 window: crisp pixels.", [2]int{1440, 810}, [2]int{480, 270}},
	{"tall", "Tall 540×960", "A phone-shaped window, for vertical games.", [2]int{540, 960}, [2]int{540, 960}},
}

// newGameForm asks what to start from (when there are examples), then the
// name and, for an empty game, the picture's shape.
func (s *mcpServer) newGameForm(home string) (newGameChoice, formOutcome) {
	var choice newGameChoice
	list, _ := examples.List(version, io.Discard) // none in a build that isn't a release
	if len(list) > 0 {
		options := []map[string]any{option("empty", "Empty game", "A starter scene and a player script, yours to build on.", "")}
		for _, ex := range list {
			options = append(options, option(ex.Name, ex.Title, readmeLine(ex.Path), dataURI(projects.Thumb(ex.Path), 320)))
		}
		answer, outcome := s.ask("Start a new game: from nothing, or from one of the examples to change.",
			map[string]any{"from": choiceField("Start from", "empty", options...)})
		if outcome != formAnswered {
			return choice, outcome
		}
		if from, _ := answer["from"].(string); from != "empty" {
			choice.from = from
		}
	}
	base := "My Game"
	if i := slices.IndexFunc(list, func(ex examples.Example) bool { return ex.Name == choice.from }); i >= 0 {
		base = list[i].Title
	}
	suggested := freeGameName(home, base)
	fields := map[string]any{"name": map[string]any{"type": "string", "title": "Name", "minLength": 1, "default": suggested,
		"description": "Its window title, and its folder in " + home + ".", "x-openai-suggestions": []map[string]any{option(suggested, suggested, "", "")}}}
	if choice.from == "" {
		var options []map[string]any
		for _, p := range pictures {
			options = append(options, option(p.key, p.title, p.description, ""))
		}
		fields["picture"] = choiceField("Picture", pictures[0].key, options...)
	}
	answer, outcome := s.ask("Name the game.", fields, "name")
	if outcome != formAnswered {
		return choice, outcome
	}
	choice.name, _ = answer["name"].(string)
	for _, p := range pictures {
		if choice.from == "" && p.key == answer["picture"] {
			choice.settings = map[string]any{"config.window.width": p.window[0], "config.window.height": p.window[1],
				"config.renderer.logicalWidth": p.logical[0], "config.renderer.logicalHeight": p.logical[1]}
		}
	}
	return choice, formAnswered
}

// freeGameName is base, or base 2, 3… : the first with no folder in home yet.
func freeGameName(home, base string) string {
	name := base
	for n := 2; exists(filepath.Join(home, exportName(name))); n++ {
		name = fmt.Sprintf("%s %d", base, n)
	}
	return name
}

// readmeLine is the first sentence of a game's README after its title.
func readmeLine(dir string) string {
	f, err := os.Open(filepath.Join(dir, "README.md"))
	if err != nil {
		return ""
	}
	defer f.Close()
	var text []string
	for lines := bufio.NewScanner(f); lines.Scan(); {
		line := strings.TrimSpace(lines.Text())
		if strings.HasPrefix(line, "#") || line == "" && len(text) == 0 {
			continue
		}
		if line == "" {
			break
		}
		text = append(text, line)
	}
	sentence := strings.Join(text, " ")
	if i := strings.Index(sentence, ". "); i >= 0 {
		sentence = sentence[:i+1]
	}
	return sentence
}

// pickKinds are the files pick offers, by kind.
var pickKinds = map[string][]string{
	"scene": {".scene.json"}, "prefab": {".prefab.json"}, "texture": {".png", ".jpg", ".jpeg"},
	"script": {".ts"}, "sound": {".wav", ".ogg", ".mp3"}, "ui": {".ui.html"}, "map": {".tmj"},
}

func pickTool(s *mcpServer) mcpTool {
	kinds := []string{}
	for kind := range pickKinds {
		kinds = append(kinds, kind)
	}
	slices.Sort(kinds)
	return mcpTool{Name: "pick", Title: "Ask the person to pick", Meta: callableMeta("", ""),
		Description: "Ask the person to point at files of the open game (scenes, prefabs, textures, scripts, sounds, UI, maps) in a picker with previews, " +
			"when \"this one\" or \"that level\" is unclear. Gives the picked files' paths. Hosts without forms: lists the choices to ask about in chat.",
		Annotations: readOnly(), InputSchema: object(map[string]any{
			"kind":    map[string]any{"type": "string", "enum": kinds, "description": "what to pick"},
			"message": strArg("the question, e.g. \"Which level feels too long?\""),
			"many":    boolArg("let them pick several"),
		}, "kind"),
		run: func(a toolArgs) toolResult {
			suffixes, ok := pickKinds[a.str("kind")]
			if !ok {
				return textResult("kind: one of "+strings.Join(kinds, ", "), true)
			}
			if !exists(archive.ManifestEntryKey) {
				return textResult("No game open: open_game first.", true)
			}
			files := gameFiles(func(path string) bool {
				_, err := agentFile(path) // not a link out of the game
				return err == nil && slices.ContainsFunc(suffixes, func(suffix string) bool { return strings.HasSuffix(path, suffix) })
			})
			if len(files) == 0 {
				return textResult("The game has no "+a.str("kind")+" files.", false)
			}
			files = files[:min(len(files), 200)]
			game, _ := os.Getwd()
			byURI := map[string]string{} // made now: a call while the person picks may open another game
			for _, f := range files {
				abs, _ := filepath.Abs(f)
				byURI[fileURI(abs)] = f
			}
			message := cmp.Or(a.str("message"), "Pick the "+a.str("kind")+" you mean.")
			answer, outcome := s.ask(message, map[string]any{"picked": pickField(files, a.flag("many"))}, "picked")
			switch outcome {
			case formUnavailable:
				return textResult("This host shows no picker: ask the person in chat which of these they mean:\n"+strings.Join(files, "\n"), false)
			case formDeclined:
				return textResult("The person closed the picker without picking.", false)
			}
			var picked []string
			for _, uri := range answers(answer["picked"]) {
				if f, ok := byURI[uri]; ok {
					picked = append(picked, f)
				}
			}
			return toolResult{Text: "The person picked: " + cmp.Or(strings.Join(picked, ", "), "nothing") + " (in " + game + ")",
				Structured: map[string]any{"picked": picked, "game": game}}
		}}
}

// pickField is a picker of files: each a resource with its picture (a
// texture's own) and a preview (a scene opens in scene_view).
func pickField(files []string, many bool) map[string]any {
	var options []map[string]any
	for _, f := range files {
		abs, _ := filepath.Abs(f)
		meta := map[string]any{}
		if mimeOf(f) != "" { // a resource jm serves
			meta["openai/preview"] = map[string]any{"target": map[string]any{"type": "resource_link", "uri": fileURI(abs), "name": f}}
		}
		if strings.HasSuffix(f, ".scene.json") {
			meta["openai/preview"] = map[string]any{"target": map[string]any{"type": "mcp_app_tool", "name": "scene_view", "arguments": map[string]any{"scene": f}}}
		}
		if slices.Contains(pickKinds["texture"], filepath.Ext(f)) {
			if thumb := dataURI(f, 160); thumb != "" {
				meta["openai/thumbnail"] = map[string]any{"src": thumb}
			}
		}
		options = append(options, map[string]any{"uri": fileURI(abs), "name": f, "title": filepath.Base(f), "mimeType": cmp.Or(mimeOf(f), "application/octet-stream"), "_meta": meta})
	}
	input := map[string]any{"type": "resource", "options": options}
	if !many {
		return map[string]any{"type": "string", "format": "uri", "title": "File", "x-openai-input": input}
	}
	input["selection"] = "explicit"
	return map[string]any{"type": "array", "items": map[string]any{"type": "string", "format": "uri"}, "title": "Files", "x-openai-input": input}
}
