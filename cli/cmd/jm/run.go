package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

var runCmd = &cobra.Command{
	Use:   "run [build path]",
	Short: "Run the Journeyman game engine",
	Args:  cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		buildPath := args[0]

		// Load the manifest
		man, err := manifest.LoadManifest(filepath.Join(buildPath, ".jm.json"))
		if err != nil {
			fmt.Printf("Failed to load manifest from %s: %s\n", buildPath, err)
			os.Exit(1)
		}

		enginePath, err := resolveEnginePath(man.EnginePath, filepath.Join(buildPath, ".jm.json"))
		if err != nil {
			fmt.Printf("Engine binary not found: %s\n", err)
			os.Exit(1)
		}

		fmt.Printf("Running engine: %s with build: %s\n", enginePath, buildPath)

		engineCmd := exec.Command(enginePath, buildPath)
		engineCmd.Stdout = os.Stdout
		engineCmd.Stderr = os.Stderr

		if err := engineCmd.Run(); err != nil {
			fmt.Printf("Error running engine: %s\n", err)
			os.Exit(1)
		}
	},
}

func resolveEnginePath(enginePath string, manifestPath string) (string, error) {
	if filepath.IsAbs(enginePath) {
		if _, err := os.Stat(enginePath); err == nil {
			return enginePath, nil
		}
		return "", fmt.Errorf("engine not found at absolute path: %s", enginePath)
	}

	manifestDir := filepath.Dir(manifestPath)
	fullPath := filepath.Join(manifestDir, enginePath)
	if _, err := os.Stat(fullPath); err == nil {
		return fullPath, nil
	}

	engineExec, err := exec.LookPath(enginePath)
	if err == nil {
		return engineExec, nil
	}

	return "", fmt.Errorf("could not resolve engine path: %s", enginePath)
}
