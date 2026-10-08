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

// The entry scene a new project starts with.
const initEntryScenePath = "scenes/main.scene.json"

// Ensured in a new project's .gitignore: jm build's output (and its staging
// folders), jm export's, archives, engine logs, stray nested npm installs and
// common editor caches.
var defaultGitignoreLines = []string{
	".vscode/",
	".cache/",
	"build/",
	"build.next/",
	"build.old/",
	"dist/",
	"logs/",
	"node_modules/",
	"*.jm",
}

// The assets/scripts npm project (asc, LSP and gitignore setup). jm build
// syncs @jm/runtime into its node_modules, so init doesn't touch that.
const scriptsPackageJSON = `{
  "name": "scripts",
  "private": true,
  "engines": {
    "node": ">=20"
  },
  "dependencies": {
    "assemblyscript": "^0.28.17"
  }
}
`

const scriptsAsconfigJSON = `{
  "options": {
    "exportRuntime": false
  }
}
`

const scriptsTsconfigJSON = `{
  "extends": "assemblyscript/std/assembly.json",
  "include": ["./**/*.ts"]
}
`

const scriptsGitignore = `node_modules/
*.wasm
`

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
		EnginePath: "journeyman_engine", // found on $PATH at run time
		EntryScene: initEntryScenePath,
		Scenes:     []string{initEntryScenePath},
		// Everything under assets/ builds; new files need no manifest edit.
		Assets: []string{"assets/**"},
		Config: map[string]any{
			"window":   map[string]any{"width": 1280, "height": 720},
			"renderer": map[string]any{"logicalWidth": 1280, "logicalHeight": 720},
		},
	}
	manData, err := json.MarshalIndent(man, "", "  ")
	if err != nil {
		return fmt.Errorf("init: marshal manifest: %w", err)
	}
	if err := os.WriteFile(manifestPath, append(manData, '\n'), 0o644); err != nil {
		return fmt.Errorf("init: write manifest: %w", err)
	}
	fmt.Fprintf(out, "Created %s\n", manifestPath)

	// Existing files are kept, so re-running init never clobbers user content.
	scriptsDir := filepath.Join(projectDir, filepath.FromSlash(scriptsPkgDir))
	scaffold := []struct{ path, body string }{
		{filepath.Join(projectDir, initEntryScenePath), bodyNamed(sceneTemplate, "main")},
		{filepath.Join(scriptsDir, "package.json"), scriptsPackageJSON},
		{filepath.Join(scriptsDir, "asconfig.json"), scriptsAsconfigJSON},
		{filepath.Join(scriptsDir, "tsconfig.json"), scriptsTsconfigJSON},
		{filepath.Join(scriptsDir, ".gitignore"), scriptsGitignore},
	}
	for _, f := range scaffold {
		created, err := writeIfMissing(f.path, []byte(f.body))
		if err != nil {
			return fmt.Errorf("init: write %s: %w", f.path, err)
		}
		if created {
			fmt.Fprintf(out, "Created %s\n", f.path)
		} else {
			fmt.Fprintf(out, "Kept existing %s\n", f.path)
		}
	}

	gitignoreUpdated, err := ensureGitignoreLines(projectDir, defaultGitignoreLines)
	if err != nil {
		return fmt.Errorf("init: update .gitignore: %w", err)
	}
	if gitignoreUpdated {
		fmt.Fprintf(out, "Updated %s\n", filepath.Join(projectDir, ".gitignore"))
	}

	fmt.Fprintf(out, "\nNext steps (building scripts needs Node.js %d+):\n", minNodeMajor)
	fmt.Fprintf(out, "  jm generate script <name>   # add a script\n")
	fmt.Fprintf(out, "  jm build                    # compile and assemble build/ (the first one installs the script packages)\n")
	fmt.Fprintf(out, "  jm run                      # play it\n")
	return nil
}

// writeIfMissing writes data to path (making its folder) unless the file
// already exists. Returns whether it wrote.
func writeIfMissing(path string, data []byte) (bool, error) {
	if _, err := os.Stat(path); err == nil {
		return false, nil
	} else if !os.IsNotExist(err) {
		return false, err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return false, err
	}
	return true, os.WriteFile(path, data, 0o644)
}
