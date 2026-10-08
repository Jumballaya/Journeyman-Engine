// Package docs carries the engine's guides inside jm, so a machine with only
// the CLI (an agent's container) can read them: `jm docs <topic>`, and the
// AGENTS.md that `jm init` writes. md/ holds copies of the repo's docs/;
// `go generate ./internal/docs` refreshes them, and a test fails when they
// drift.
package docs

//go:generate sh -c "rm -f md/*.md && cp ../../../docs/agents.md ../../../docs/content.md ../../../docs/editor.md ../../../docs/performance.md ../../../docs/runtime-gameplay.md ../../../docs/scripting.md ../../../docs/testing.md md/"

import (
	"embed"
	"fmt"
	"io/fs"
	"sort"
	"strings"
)

//go:embed md/*.md
var files embed.FS

// Topic is one guide: its name (the file's, without .md) and title (its first heading).
type Topic struct {
	Name  string `json:"name"`
	Title string `json:"title"`
}

// Topics lists the guides, by name.
func Topics() []Topic {
	entries, _ := fs.ReadDir(files, "md")
	var out []Topic
	for _, e := range entries {
		name := strings.TrimSuffix(e.Name(), ".md")
		text, _ := Read(name)
		title, _, _ := strings.Cut(text, "\n")
		title = strings.ReplaceAll(title, "{{name}}", "Your game") // agents.md is AGENTS.md's template
		out = append(out, Topic{name, strings.TrimSpace(strings.TrimLeft(title, "# "))})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

// Read returns a guide's markdown.
func Read(name string) (string, error) {
	data, err := files.ReadFile("md/" + name + ".md")
	if err != nil {
		names := []string{}
		for _, t := range Topics() {
			names = append(names, t.Name)
		}
		return "", fmt.Errorf("no topic %q; there are: %s", name, strings.Join(names, ", "))
	}
	return string(data), nil
}

// AgentGuide is AGENTS.md for a project named name.
func AgentGuide(name string) string {
	text, _ := Read("agents")
	return strings.ReplaceAll(text, "{{name}}", name)
}
