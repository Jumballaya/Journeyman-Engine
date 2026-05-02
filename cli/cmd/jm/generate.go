package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/spf13/cobra"
)

// generator describes a single thing `jm generate` knows how to scaffold.
// Adding a new kind = append one entry; the `list` subcommand and the parent
// command's help both read from this slice.
type generator struct {
	kind    string // "script", "prefab", "scene"
	dir     string // default output directory relative to cwd
	suffix  string // file extension appended after stripping/normalizing the name
	summary string // one-line description shown in `generate list`
	body    string // file contents written verbatim
}

var generators = []generator{
	{
		kind:    "script",
		dir:     "assets/scripts",
		suffix:  ".ts",
		summary: "AssemblyScript source with an empty onUpdate",
		body: `export function onUpdate(dt: f32): void {
}
`,
	},
	{
		kind:    "prefab",
		dir:     "assets/prefabs",
		suffix:  ".prefab.json",
		summary: "Empty prefab with no components or tags",
		body: `{
  "components": {},
  "tags": []
}
`,
	},
	{
		kind:    "scene",
		dir:     "scenes",
		suffix:  ".scene.json",
		summary: "Empty scene with no entities",
		body: `{
  "name": "",
  "entities": []
}
`,
	},
}

var generateCmd = &cobra.Command{
	Use:   "generate",
	Short: "Scaffold a new script, prefab, or scene",
	Long:  "Create empty source files for the project. Run `jm generate list` to see what can be generated.",
}

func init() {
	generateCmd.AddCommand(generateListCmd)
	for _, g := range generators {
		generateCmd.AddCommand(newGenerateSubcommand(g))
	}
}

var generateListCmd = &cobra.Command{
	Use:   "list",
	Short: "List the kinds of files `jm generate` can create",
	RunE: func(cmd *cobra.Command, args []string) error {
		return runGenerateList(cmd.OutOrStdout())
	},
}

func runGenerateList(out io.Writer) error {
	sorted := append([]generator(nil), generators...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].kind < sorted[j].kind })
	for _, g := range sorted {
		fmt.Fprintf(out, "%-7s  %s/<name>%s  — %s\n", g.kind, g.dir, g.suffix, g.summary)
	}
	return nil
}

func newGenerateSubcommand(g generator) *cobra.Command {
	return &cobra.Command{
		Use:   g.kind + " <name>",
		Short: "Generate a new " + g.kind,
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return runGenerate(g, args[0], cmd.OutOrStdout())
		},
	}
}

func runGenerate(g generator, rawName string, out io.Writer) error {
	if _, err := os.Stat(archive.ManifestEntryKey); err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("generate %s: no %s in current directory — run from a Journeyman project root", g.kind, archive.ManifestEntryKey)
		}
		return fmt.Errorf("generate %s: stat %s: %w", g.kind, archive.ManifestEntryKey, err)
	}
	name := strings.TrimSpace(rawName)
	if name == "" {
		return fmt.Errorf("generate %s: name is required", g.kind)
	}
	// Strip the suffix if the user typed it — both `weapon` and `weapon.prefab.json`
	// should produce the same path. Done before path-cleaning so a trailing
	// suffix on a nested name (`weapons/sword.prefab.json`) is handled too.
	name = strings.TrimSuffix(name, g.suffix)
	cleaned := filepath.Clean(filepath.FromSlash(name))
	if cleaned == "." || strings.HasPrefix(cleaned, "..") || filepath.IsAbs(cleaned) {
		return fmt.Errorf("generate %s: invalid name %q", g.kind, rawName)
	}

	relPath := filepath.Join(g.dir, cleaned+g.suffix)
	if _, err := os.Stat(relPath); err == nil {
		return fmt.Errorf("generate %s: %s already exists", g.kind, relPath)
	} else if !os.IsNotExist(err) {
		return fmt.Errorf("generate %s: stat %s: %w", g.kind, relPath, err)
	}

	if err := os.MkdirAll(filepath.Dir(relPath), 0o755); err != nil {
		return fmt.Errorf("generate %s: mkdir %s: %w", g.kind, filepath.Dir(relPath), err)
	}
	if err := os.WriteFile(relPath, []byte(g.body), 0o644); err != nil {
		return fmt.Errorf("generate %s: write %s: %w", g.kind, relPath, err)
	}

	fmt.Fprintf(out, "Created %s\n", relPath)

	field := manifestFieldFor(g.kind)
	manifestKey := filepath.ToSlash(relPath)
	added, err := addToManifestArray(archive.ManifestEntryKey, field, manifestKey)
	if err != nil {
		return fmt.Errorf("generate %s: register in %s: %w", g.kind, archive.ManifestEntryKey, err)
	}
	if added {
		fmt.Fprintf(out, "Registered in %s (%s[])\n", archive.ManifestEntryKey, field)
	} else {
		fmt.Fprintf(out, "Already listed in %s (%s[])\n", archive.ManifestEntryKey, field)
	}
	return nil
}

func manifestFieldFor(kind string) string {
	switch kind {
	case "scene":
		return "scenes"
	default:
		return "assets"
	}
}

// addToManifestArray appends `value` to the manifest's named string array,
// preserving existing entries and other fields. Returns true if the value was
// newly added (false if it was already present). Mirrors migrate.go's
// map[string]interface{} round-trip so on-disk formatting (alphabetical keys,
// 2-space indent) stays consistent across CLI commands that mutate the manifest.
func addToManifestArray(manifestPath, field, value string) (bool, error) {
	data, err := os.ReadFile(manifestPath)
	if err != nil {
		return false, err
	}
	var raw map[string]interface{}
	if err := json.Unmarshal(data, &raw); err != nil {
		return false, err
	}

	var current []string
	if existing, ok := raw[field].([]interface{}); ok {
		for _, e := range existing {
			if s, ok := e.(string); ok {
				current = append(current, s)
			}
		}
	}
	for _, s := range current {
		if s == value {
			return false, nil
		}
	}
	current = append(current, value)
	sort.Strings(current)

	asIface := make([]interface{}, len(current))
	for i, s := range current {
		asIface[i] = s
	}
	raw[field] = asIface

	out, err := json.MarshalIndent(raw, "", "  ")
	if err != nil {
		return false, err
	}
	out = append(out, '\n')
	if err := os.WriteFile(manifestPath, out, 0o644); err != nil {
		return false, err
	}
	return true, nil
}
