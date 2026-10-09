package main

import (
	"encoding/json"
	"fmt"
	"maps"
	"os"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/schema"

	"github.com/spf13/cobra"
)

var schemaEngine string

var schemaCmd = &cobra.Command{
	Use:   "schema [component]",
	Short: "Print the engine's component schema as JSON",
	Long: `Prints every component the engine knows, as JSON: each one's scene/prefab
keys (kind, default, hint, choices, accepted asset types) and the fields
scripts can reach. With a component name, just that one.

The engine is the project's (its .jm.json "engine"), else journeyman_engine
beside jm or on $PATH; --engine picks one. jm build checks scenes and prefabs
against this schema.

Script fields are the engine's own names; scripts reach them through
@jm/runtime's wrappers, which may name them differently (VelocityComponent's
vx is entity.velocity.x): see jm docs scripting.`,
	Args: cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		engine, err := projectEngine(schemaEngine)
		if err != nil {
			return err
		}
		raw, err := schema.Raw(engine)
		if err != nil {
			return err
		}
		if len(args) == 0 {
			fmt.Print(string(raw))
			return nil
		}
		s, err := schema.Parse(raw)
		if err != nil {
			return err
		}
		c, ok := s.Components[args[0]]
		if !ok {
			names := slices.Sorted(maps.Keys(s.Components))
			return fmt.Errorf("no component %q; the engine has: %s", args[0], strings.Join(names, ", "))
		}
		out, _ := json.MarshalIndent(c, "", "  ")
		fmt.Println(string(out))
		return nil
	},
}

func init() {
	schemaCmd.Flags().StringVar(&schemaEngine, "engine", "", "the engine binary to ask (default: the project's)")
}

// projectEngine finds the engine to ask: an explicit path, else the one the
// project in the current folder names, else the default lookup.
func projectEngine(explicit string) (string, error) {
	if explicit != "" {
		return resolveEnginePath(explicit, archive.ManifestEntryKey)
	}
	enginePath := ""
	if _, err := os.Stat(archive.ManifestEntryKey); err == nil {
		if man, err := manifest.LoadManifest(archive.ManifestEntryKey); err == nil {
			enginePath = man.EnginePath
		}
	}
	return resolveEnginePath(enginePath, archive.ManifestEntryKey)
}

// checkContent checks the scenes and prefabs being built against the engine's
// schema, printing what's wrong. It warns rather than fails: an older engine
// may not know a newer key. Without an engine to ask, it says so and skips.
func checkContent(enginePath string, files []string) {
	engine, err := resolveEnginePath(enginePath, archive.ManifestEntryKey)
	var s *schema.Schema
	if err == nil {
		s, err = schema.FromEngine(engine)
	}
	if err != nil {
		say("Skipped checking scenes and prefabs (no engine schema): %v", err)
		return
	}
	var problems []schema.Problem
	for _, f := range files {
		data, err := os.ReadFile(f)
		if err != nil {
			problems = append(problems, schema.Problem{File: f, Message: err.Error()})
			continue
		}
		problems = append(problems, s.Check(f, data)...)
	}
	for _, p := range problems {
		message := p.Message
		if p.Where != "" {
			message = p.Where + ": " + message
		}
		emit(Diagnostic{Level: "warning", Category: "content", File: p.File, Line: p.Line, Column: p.Column, Message: message})
	}
	if len(problems) > 0 {
		say("%d problem(s) in scenes and prefabs (checked against %s --schema)", len(problems), engine)
	}
}
