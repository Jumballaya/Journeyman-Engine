package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

// Default scene path written by `jm init` and referenced from the new manifest.
// Matches the layout `jm generate scene` would produce.
const initEntryScenePath = "scenes/main.scene.json"

// Default engine reference: the binary is looked up via $PATH at run time
// (resolveEnginePath), matching the demo's manifest. Users can edit to use an
// absolute path or a path relative to the project root.
const initDefaultEngine = "journeyman_engine"

var initCmd = &cobra.Command{
	Use:   "init [name]",
	Short: "Bootstrap a new Journeyman project in the current directory",
	Long: `Creates .jm.json, scenes/main.scene.json, and ensures build/ + *.jm are gitignored.

If [name] is omitted, the project name defaults to the current directory's basename.
Refuses to run if .jm.json already exists.`,
	Args: cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		name := ""
		if len(args) == 1 {
			name = args[0]
		}
		return runInit(".", name, cmd.OutOrStdout())
	},
}

func runInit(projectDir, name string, out io.Writer) error {
	manifestPath := filepath.Join(projectDir, archive.ManifestEntryKey)
	if _, err := os.Stat(manifestPath); err == nil {
		return fmt.Errorf("init: %s already exists in %s", archive.ManifestEntryKey, projectDir)
	} else if !os.IsNotExist(err) {
		return fmt.Errorf("init: stat %s: %w", manifestPath, err)
	}

	if name == "" {
		abs, err := filepath.Abs(projectDir)
		if err != nil {
			return fmt.Errorf("init: resolve project dir: %w", err)
		}
		name = filepath.Base(abs)
	}

	man := manifest.GameManifest{
		Name:       name,
		Version:    "0.1.0",
		EnginePath: initDefaultEngine,
		EntryScene: initEntryScenePath,
		Scenes:     []string{initEntryScenePath},
		Assets:     []string{},
	}
	manData, err := json.MarshalIndent(man, "", "  ")
	if err != nil {
		return fmt.Errorf("init: marshal manifest: %w", err)
	}
	manData = append(manData, '\n')
	if err := os.WriteFile(manifestPath, manData, 0o644); err != nil {
		return fmt.Errorf("init: write manifest: %w", err)
	}
	fmt.Fprintf(out, "Created %s\n", manifestPath)

	scenePath := filepath.Join(projectDir, initEntryScenePath)
	sceneCreated, err := writeIfMissing(scenePath, []byte(emptySceneBody()))
	if err != nil {
		return fmt.Errorf("init: write entry scene: %w", err)
	}
	if sceneCreated {
		fmt.Fprintf(out, "Created %s\n", scenePath)
	} else {
		fmt.Fprintf(out, "Kept existing %s\n", scenePath)
	}

	gitignoreUpdated, err := ensureGitignoreLines(projectDir, []string{"build/", "*.jm"})
	if err != nil {
		return fmt.Errorf("init: update .gitignore: %w", err)
	}
	if gitignoreUpdated {
		fmt.Fprintf(out, "Updated %s\n", filepath.Join(projectDir, ".gitignore"))
	}

	return nil
}

// writeIfMissing creates the parent dir and writes `data` to `path` only if
// the file doesn't already exist. Returns true if a write occurred.
// Init reuses the existing scene if one is there so repeated init-in-place
// doesn't clobber user content.
func writeIfMissing(path string, data []byte) (bool, error) {
	if _, err := os.Stat(path); err == nil {
		return false, nil
	} else if !os.IsNotExist(err) {
		return false, err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return false, err
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		return false, err
	}
	return true, nil
}

// emptySceneBody returns the same scaffold body as `jm generate scene` so the
// two stay in lockstep — sourced from the generators table to avoid drift.
func emptySceneBody() string {
	for _, g := range generators {
		if g.kind == "scene" {
			return g.body
		}
	}
	return "{}\n"
}
