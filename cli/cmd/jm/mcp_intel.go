package main

import (
	"encoding/json"
	"fmt"
	"path/filepath"
	"slices"
	"strings"
)

// Project intelligence: the game's files and how they link (game_index), and
// whether it's healthy (health: doctor, build, tests, goldens in one answer).
func (s *mcpServer) intelTools() []mcpTool {
	return []mcpTool{
		{Name: "game_index", Title: "The game's files and how they link",
			Description: "The open game at a glance: every scene, prefab, script, asset, map, UI file, design doc and test, " +
				"with what each uses and what uses it (\"used by\"), and the files nothing uses. " +
				"With path: that file's links (and a picture of an image). Call it before changing or removing a shared file.",
			Annotations: readOnly(), Meta: callableMeta("", ""),
			InputSchema: object(map[string]any{"path": strArg("a file of the game, e.g. assets/textures/ship.png (default: the whole game)")}),
			run:         gameIndex},
		{Name: "health", Title: "Is the game healthy?",
			Description: "Checks the open game: doctor (setup), build (scripts and content), tests (tests/*.spec.ts) and goldens (frames), " +
				"each with its problems and where they are. checks picks some (build and goldens take a while).",
			Annotations: writes(), Meta: callableMeta("Checking the game…", "Checked"),
			InputSchema: object(map[string]any{"checks": listArg(`some of "doctor", "build", "tests", "goldens" (default: all)`)}),
			run:         health},
	}
}

func gameIndex(a toolArgs) toolResult {
	root, err := projectRoot()
	if err != nil {
		return textResult(err.Error(), true)
	}
	entries := indexGame(root)
	if a.str("path") == "" {
		return toolResult{Text: describeIndex(gameName(root), entries), Structured: map[string]any{"game": gameName(root), "entries": entries}}
	}
	rel, err := projectFile(a.str("path"))
	i := slices.IndexFunc(entries, func(e indexEntry) bool { return e.Path == filepath.ToSlash(rel) })
	if err != nil || i < 0 {
		return textResult(fmt.Sprintf("%s isn't one of the game's files (game_index lists them)", a.str("path")), true)
	}
	e := entries[i]
	text := fmt.Sprintf("%s (%s%s). Uses: %s. Used by: %s.", e.Path, e.Kind, map[bool]string{true: ", " + e.Detail}[e.Detail != ""],
		listOr(e.Uses, "nothing"), listOr(e.UsedBy, "nothing"))
	result := toolResult{Text: text, Structured: map[string]any{"entry": e}}
	if e.Kind == "texture" {
		if uri := dataURI(filepath.Join(root, filepath.FromSlash(e.Path)), 480); uri != "" {
			result.Meta = map[string]any{"jm/image": uri}
		}
	}
	return result
}

func listOr(list []string, none string) string {
	if len(list) == 0 {
		return none
	}
	return strings.Join(list, ", ")
}

// describeIndex sums the game up for the model: files by kind, and the unused.
func describeIndex(name string, entries []indexEntry) string {
	counts := map[string]int{}
	var kinds, unused []string
	for _, e := range entries {
		if counts[e.Kind]++; counts[e.Kind] == 1 {
			kinds = append(kinds, e.Kind)
		}
		if e.Unused {
			unused = append(unused, e.Path)
		}
	}
	var b strings.Builder
	fmt.Fprintf(&b, "%s:", name)
	for _, k := range kinds {
		fmt.Fprintf(&b, " %d %s,", counts[k], k)
	}
	text := strings.TrimSuffix(b.String(), ",") + "."
	if len(unused) > 0 {
		shown := unused[:min(len(unused), 20)]
		text += fmt.Sprintf(" Nothing uses %d of them: %s", len(unused), strings.Join(shown, ", "))
		if len(unused) > len(shown) {
			text += ", …"
		}
		text += "."
	}
	return text + " structuredContent has each file's uses and used-by; game_index with path for one."
}

// healthCheck is one check's outcome: ok, a line saying so, and its problems.
type healthCheck struct {
	Name     string       `json:"name"`
	OK       bool         `json:"ok"`
	Summary  string       `json:"summary"`
	Problems []Diagnostic `json:"problems"`
}

func health(a toolArgs) toolResult {
	root, err := projectRoot()
	if err != nil {
		return textResult(err.Error(), true)
	}
	checks := a.list("checks")
	if len(checks) == 0 {
		checks = []string{"doctor", "build", "tests", "goldens"}
	}
	var out []healthCheck
	var lines []string
	for _, name := range checks {
		c := runCheck(root, name)
		out = append(out, c)
		lines = append(lines, fmt.Sprintf("%s: %s", c.Name, c.Summary))
		for _, p := range c.Problems {
			where := p.File
			if p.Line > 0 {
				where += fmt.Sprintf(":%d", p.Line)
			}
			lines = append(lines, fmt.Sprintf("  %s %s %s", p.Level, where, p.Message))
		}
	}
	return toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"checks": out}}
}

// runCheck runs one check with jm's --json output and reads its problems back.
func runCheck(root, name string) healthCheck {
	c := healthCheck{Name: name, Problems: []Diagnostic{}}
	var args []string
	switch name {
	case "doctor":
		args = []string{"doctor", "--json"}
	case "build":
		args = []string{"build", "--json"}
	case "tests":
		if specs, _ := filepath.Glob(filepath.Join(root, "tests", "*.spec.ts")); len(specs) == 0 {
			c.OK, c.Summary = true, "no tests yet (tests/*.spec.ts)"
			return c
		}
		args = []string{"test", "--json"}
	case "goldens":
		if specs, _ := filepath.Glob(filepath.Join(root, goldenDir, "*.golden.json")); len(specs) == 0 {
			c.OK, c.Summary = true, "no goldens yet (" + goldenDir + "/*.golden.json)"
			return c
		}
		if buildIsStale(root) {
			if text, failed := jmIn(root, "build", "--json"); failed {
				c = readCheck(name, text, true)
				c.Summary = "the build it runs failed: " + c.Summary
				return c
			}
		}
		args = []string{"golden", "--json"}
	default:
		c.Summary = "no such check (doctor, build, tests, goldens)"
		return c
	}
	text, failed := jmIn(root, args...)
	return readCheck(name, text, failed)
}

// readCheck reads a check's --json output: its problems (doctor's in one
// object, the rest a line each) and its result line.
func readCheck(name, text string, failed bool) healthCheck {
	c := healthCheck{Name: name, OK: !failed, Problems: []Diagnostic{}}
	var last map[string]any
	if name == "doctor" { // one object: its problems
		var doc struct {
			Problems []Diagnostic `json:"problems"`
		}
		_ = json.Unmarshal([]byte(text), &doc)
		c.Problems = append(c.Problems, doc.Problems...)
	}
	for _, line := range strings.Split(text, "\n") { // problems, then the result line
		var m map[string]any
		if json.Unmarshal([]byte(line), &m) != nil {
			continue
		}
		var d Diagnostic
		if m["level"] != nil && json.Unmarshal([]byte(line), &d) == nil {
			c.Problems = append(c.Problems, d)
		} else if m["result"] != nil {
			last = m
		}
	}
	switch {
	case last != nil && last["passed"] != nil:
		c.Summary = fmt.Sprintf("%v passed, %v failed", last["passed"], last["failed"])
	case !c.OK && len(c.Problems) == 0:
		c.Summary = "failed: " + firstLine(text) // it said why in prose
	case len(c.Problems) > 0:
		counts := map[string]int{}
		for _, p := range c.Problems {
			counts[p.Level]++
		}
		var parts []string
		for _, level := range []string{"error", "warning"} {
			if n := counts[level]; n > 0 {
				parts = append(parts, fmt.Sprintf("%d %s%s", n, level, map[bool]string{true: "s"}[n != 1]))
			}
		}
		c.Summary = strings.Join(parts, ", ")
	default:
		c.Summary = "ok"
	}
	return c
}
