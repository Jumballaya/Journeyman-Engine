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
	Short: "Run the Journeyman game engine (default: ./build)",
	Args:  cobra.MaximumNArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		target := "build"
		if len(args) == 1 {
			target = args[0]
		}
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

	enginePath, err := resolveEnginePathArchive(man.EnginePath, archivePath)
	if err != nil {
		return fmt.Errorf("engine binary not found: %w", err)
	}

	fmt.Printf("Running engine: %s with archive: %s\n", enginePath, archivePath)
	engineCmd := exec.Command(enginePath, archivePath)
	engineCmd.Stdout = os.Stdout
	engineCmd.Stderr = os.Stderr
	return engineCmd.Run()
}

// resolveEnginePath finds the engine binary named by the manifest's
// `engine` field. Relative paths are tried against, in order: the build
// directory holding the manifest (legacy convention), the project root (its
// parent — the natural place to author the path from), and the current
// directory; finally the name is looked up in $PATH.
func resolveEnginePath(enginePath string, manifestPath string) (string, error) {
	buildDir := filepath.Dir(manifestPath)
	return findEngine(enginePath, []string{buildDir, filepath.Dir(buildDir), "."})
}

// resolveEnginePathArchive resolves the engine for an archive. The archive
// usually sits in the project's build/ dir, so the same search applies with
// the archive's directory standing in for the build dir.
func resolveEnginePathArchive(enginePath string, archivePath string) (string, error) {
	archiveDir := filepath.Dir(archivePath)
	return findEngine(enginePath, []string{archiveDir, filepath.Dir(archiveDir), "."})
}

func findEngine(enginePath string, bases []string) (string, error) {
	if enginePath == "" {
		enginePath = "journeyman_engine"
	}
	if filepath.IsAbs(enginePath) {
		if isFile(enginePath) {
			return enginePath, nil
		}
		return "", fmt.Errorf("engine not found at absolute path: %s", enginePath)
	}
	if strings.ContainsRune(enginePath, os.PathSeparator) || strings.Contains(enginePath, "/") {
		for _, base := range bases {
			candidate := filepath.Join(base, enginePath)
			if isFile(candidate) {
				return candidate, nil
			}
		}
	}
	if found, err := exec.LookPath(enginePath); err == nil {
		return found, nil
	}
	return "", fmt.Errorf("could not resolve engine path %q (tried relative to %s, and $PATH). "+
		"Build the engine (./scripts/build-release.sh) or set \"engine\" in .jm.json", enginePath, strings.Join(bases, ", "))
}

func isFile(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}
