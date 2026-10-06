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
		if err := runGame(target); err != nil {
			fmt.Println(err)
			os.Exit(1)
		}
	},
}

// runGame launches the engine on a build folder or a .jm archive.
func runGame(target string) error {
	manifestPath := filepath.Join(target, archive.ManifestEntryKey)
	var man manifest.GameManifest
	var err error
	kind := "build"
	if strings.HasSuffix(target, ".jm") {
		manifestPath, kind = target, "archive"
		man, err = readArchiveManifest(target)
	} else {
		man, err = manifest.LoadManifest(manifestPath)
	}
	if err != nil {
		return fmt.Errorf("failed to load manifest from %s: %w", target, err)
	}
	enginePath, err := resolveEnginePath(man.EnginePath, manifestPath)
	if err != nil {
		return fmt.Errorf("engine binary not found: %w", err)
	}
	fmt.Printf("Running engine: %s with %s: %s\n", enginePath, kind, target)
	engineCmd := exec.Command(enginePath, target)
	engineCmd.Stdout = os.Stdout
	engineCmd.Stderr = os.Stderr
	return engineCmd.Run()
}

func readArchiveManifest(path string) (manifest.GameManifest, error) {
	f, err := os.Open(path)
	if err != nil {
		return manifest.GameManifest{}, err
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil {
		return manifest.GameManifest{}, err
	}
	arc, err := archive.ReadArchive(f, info.Size())
	if err != nil {
		return manifest.GameManifest{}, err
	}
	data, err := arc.Read(archive.ManifestEntryKey)
	if err != nil {
		return manifest.GameManifest{}, fmt.Errorf("archive missing %s entry: %w", archive.ManifestEntryKey, err)
	}
	return manifest.LoadManifestFromBytes(data)
}

// resolveEnginePath finds the engine binary named by a manifest's `engine`
// field, given the manifest's file (or the archive holding it). A relative
// path is tried against that file's folder (build/, the legacy convention),
// its parent (the project root, the natural place to author it from) and the
// current directory; a bare name is looked up in $PATH.
func resolveEnginePath(enginePath, manifestPath string) (string, error) {
	if enginePath == "" {
		enginePath = "journeyman_engine"
	}
	if filepath.IsAbs(enginePath) {
		if isFile(enginePath) {
			return enginePath, nil
		}
		return "", fmt.Errorf("engine not found at absolute path: %s", enginePath)
	}
	buildDir := filepath.Dir(manifestPath)
	bases := []string{buildDir, filepath.Dir(buildDir), "."}
	if strings.ContainsRune(enginePath, os.PathSeparator) || strings.Contains(enginePath, "/") {
		for _, base := range bases {
			if candidate := filepath.Join(base, enginePath); isFile(candidate) {
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
