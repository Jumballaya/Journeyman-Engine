package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

var runCmd = &cobra.Command{
	Use:   "run [build path or .jm archive]",
	Short: "Run the Journeyman game engine",
	Args:  cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		target := args[0]
		var err error
		if strings.HasSuffix(target, ".jm") {
			err = runArchive(target)
		} else {
			err = runFolder(target)
		}
		if err != nil {
			fmt.Println(err)
			os.Exit(1)
		}
	},
}

func runFolder(buildPath string) error {
	manifestPath := filepath.Join(buildPath, archive.ManifestEntryKey)
	man, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return fmt.Errorf("failed to load manifest from %s: %w", buildPath, err)
	}
	enginePath, err := resolveEnginePath(man.EnginePath, manifestPath)
	if err != nil {
		return fmt.Errorf("engine binary not found: %w", err)
	}
	fmt.Printf("Running engine: %s with build: %s\n", enginePath, buildPath)
	engineCmd := exec.Command(enginePath, buildPath)
	engineCmd.Stdout = os.Stdout
	engineCmd.Stderr = os.Stderr
	return engineCmd.Run()
}

func runArchive(archivePath string) error {
	f, err := os.Open(archivePath)
	if err != nil {
		return fmt.Errorf("open archive: %w", err)
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return fmt.Errorf("stat archive: %w", err)
	}

	arc, err := archive.ReadArchive(f, info.Size())
	if err != nil {
		return err
	}
	manifestBytes, err := arc.Read(archive.ManifestEntryKey)
	if err != nil {
		return fmt.Errorf("archive missing %s entry: %w", archive.ManifestEntryKey, err)
	}
	man, err := manifest.LoadManifestFromBytes(manifestBytes)
	if err != nil {
		return fmt.Errorf("parse manifest from archive: %w", err)
	}

	enginePath, err := resolveEnginePathArchive(man.EnginePath)
	if err != nil {
		return fmt.Errorf("engine binary not found: %w", err)
	}

	fmt.Printf("Running engine: %s with archive: %s\n", enginePath, archivePath)
	engineCmd := exec.Command(enginePath, archivePath)
	engineCmd.Stdout = os.Stdout
	engineCmd.Stderr = os.Stderr
	return engineCmd.Run()
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

// resolveEnginePathArchive resolves the engine binary in archive mode.
// No "relative to manifest path" branch — there is no manifest path on disk.
func resolveEnginePathArchive(enginePath string) (string, error) {
	if filepath.IsAbs(enginePath) {
		if _, err := os.Stat(enginePath); err == nil {
			return enginePath, nil
		}
		return "", fmt.Errorf("absolute engine path not found: %s", enginePath)
	}
	if abs, err := exec.LookPath(enginePath); err == nil {
		return abs, nil
	}
	return "", fmt.Errorf("could not resolve engine path %q (must be absolute or in $PATH for archives)", enginePath)
}
