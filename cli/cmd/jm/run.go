package main

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

var runCmd = &cobra.Command{
	Use:   "run [build path or .jm archive]",
	Short: "Run the Journeyman game engine (default: ./build)",
	Long: `Runs the build (or a .jm archive) in the engine. The engine's JM_* variables
pass through; the ones for unattended runs:

  JM_DRIVE=1            stepped by commands on stdin, one JSON answer per line
                        (step [n], state [part...] [tag=Name], get <path>, press <Key>, quit)
  JM_RENDERER=none      no window or GL: runs with no display (a container)
  JM_HEADLESS=1         a hidden window, with GL: frames can be captured
  JM_STRICT=1           the first error ends the run with exit code 1
  JM_EXIT_AFTER_FRAMES=n, JM_CAPTURE_DIR + JM_CAPTURE_FRAMES, JM_DUMP_DIR,
  JM_INPUT_REPLAY, JM_ERRORS, JM_SEED ...: jm docs testing has them all.

  printf 'step 60\npress Enter\nstep 60\nstate session\nquit\n' | JM_DRIVE=1 JM_RENDERER=none jm run`,
	Args: cobra.MaximumNArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		target := "build"
		if len(args) == 1 {
			target = args[0]
		}
		if err := runGame(target); err != nil {
			// The game's own exit code (JM_STRICT's 1) passes through as is.
			var exit *exec.ExitError
			if errors.As(err, &exit) {
				os.Exit(exit.ExitCode())
			}
			fmt.Fprintln(os.Stderr, err)
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
	// A build is a folder; anything else is an archive (whatever its name).
	if info, statErr := os.Stat(target); statErr == nil && !info.IsDir() {
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
	// stderr: stdout is the game's, e.g. the driver's JSON lines (JM_DRIVE).
	fmt.Fprintf(os.Stderr, "Running engine: %s with %s: %s\n", enginePath, kind, target)
	engineCmd := exec.Command(enginePath, target)
	engineCmd.Stdin = os.Stdin
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
// current directory. A bare name (the default) is looked for beside jm first,
// where a release puts the engine it was built with, then in $PATH.
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
	bases := []string{}
	for _, b := range []string{buildDir, filepath.Dir(buildDir), "."} {
		if !slices.Contains(bases, filepath.Clean(b)) {
			bases = append(bases, filepath.Clean(b)) // "build/.." and "." are one folder
		}
	}
	if strings.ContainsRune(enginePath, os.PathSeparator) || strings.Contains(enginePath, "/") {
		for _, base := range bases {
			if candidate := filepath.Join(base, enginePath); isFile(candidate) {
				return candidate, nil
			}
		}
	}
	if !strings.ContainsAny(enginePath, `/\`) {
		if self, err := executablePath(); err == nil {
			beside := filepath.Join(filepath.Dir(self), enginePath)
			if runtime.GOOS == "windows" && filepath.Ext(beside) == "" {
				beside += ".exe"
			}
			if isFile(beside) {
				return beside, nil
			}
		}
	}
	if found, err := exec.LookPath(enginePath); err == nil {
		return found, nil
	}
	return "", fmt.Errorf("could not resolve engine path %q (tried relative to %s, beside jm, and $PATH). "+
		"Keep journeyman_engine next to jm (as a release has it), or set \"engine\" in .jm.json", enginePath, strings.Join(bases, ", "))
}

// executablePath is os.Executable, replaceable in tests.
var executablePath = os.Executable

func isFile(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}
