package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"

	"github.com/spf13/cobra"
)

var editorCmd = &cobra.Command{
	Use:   "editor [folder]",
	Short: "Open a game in the editor (default: the game in this folder)",
	Long: `Starts the Journeyman editor on a game: the folder given, else the current
one if it's a game, else the editor's start screen. It returns at once; the
editor runs on its own.

The editor is $JM_EDITOR, else the one beside jm (the editor's download has
jm inside), else the one install.sh --editor put in ~/Applications (macOS) or
~/.jm/editor (Linux, Windows).`,
	Args: cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		editor, err := findEditor()
		if err != nil {
			return err
		}
		game := ""
		if len(args) == 1 {
			game = args[0]
		} else if exists(archive.ManifestEntryKey) {
			game = "."
		}
		if game != "" {
			if game, err = filepath.Abs(game); err != nil {
				return err
			}
			if !exists(filepath.Join(game, archive.ManifestEntryKey)) {
				return fmt.Errorf("%s isn't a game (no %s): jm init makes one", game, archive.ManifestEntryKey)
			}
		}
		if err := startEditor(editor, game); err != nil {
			return err
		}
		fmt.Fprintf(cmd.OutOrStdout(), "Opened the editor (%s)\n", editor)
		return nil
	},
}

// findEditor is the editor's program: $JM_EDITOR, beside jm, or where
// install.sh --editor puts it.
func findEditor() (string, error) {
	name := exeName("journeyman_editor")
	candidates := []string{os.Getenv("JM_EDITOR")}
	if self, err := executablePath(); err == nil {
		bin := filepath.Dir(self) // and install.sh's <dir>/editor beside <dir>/bin
		candidates = append(candidates, filepath.Join(bin, name), filepath.Join(bin, "..", "editor", name))
	}
	home, _ := os.UserHomeDir()
	if runtime.GOOS == "darwin" {
		for _, apps := range []string{filepath.Join(home, "Applications"), "/Applications"} {
			candidates = append(candidates, filepath.Join(apps, "Journeyman Editor.app", "Contents", "MacOS", name))
		}
	}
	candidates = append(candidates, filepath.Join(home, ".jm", "editor", name))
	for _, path := range candidates {
		if path != "" && isFile(path) {
			return filepath.Abs(path) // a bare relative name would be looked up on PATH
		}
	}
	return "", fmt.Errorf("no editor found: install it with the install line's --editor (macOS, Linux), or set JM_EDITOR")
}

// startEditor runs the editor on its own: it outlives jm and the terminal.
func startEditor(editor, game string) error {
	var args []string
	if game != "" {
		args = append(args, game)
	}
	cmd := exec.Command(editor, args...)
	cmd.SysProcAttr = detached()
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("start the editor: %w", err)
	}
	return cmd.Process.Release()
}
