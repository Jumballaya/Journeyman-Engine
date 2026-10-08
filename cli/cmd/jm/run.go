package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

var runFlags runOptions

var runCmd = &cobra.Command{
	Use:   "run [build path or .jm archive]",
	Short: "Run the Journeyman game engine (default: ./build)",
	Long: `Runs the game (default: ./build).

Multiplayer (.jm.json "net"; see docs/networking.md):
  --server       run the dedicated server (journeyman_server) instead of the game
  --host         the game hosts a session; --join host:port joins one
  --peers N      a whole session on this machine: with net.topology "server",
                 a server and N games joining it; with "p2p", N games, the
                 first hosting. Each game gets its own save folder, and
                 output folders and files named in JM_CAPTURE_DIR, JM_DUMP_DIR,
                 JM_NET_TRACE and JM_ERRORS get a per-peer suffix (peer1...).
                 JM_INPUT_REPLAY may say {peer}: replay.{peer}.txt.
  --port P       the session's UDP port (default: net.port, else 7777)`,
	Args: cobra.MaximumNArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		target := "build"
		if len(args) == 1 {
			target = args[0]
		}
		if err := runWith(target, runFlags); err != nil {
			fmt.Println(err)
			os.Exit(1)
		}
	},
}

type runOptions struct {
	server, host bool
	join         string
	peers, port  int
}

func init() {
	runCmd.Flags().BoolVar(&runFlags.server, "server", false, "Run the dedicated multiplayer server")
	runCmd.Flags().BoolVar(&runFlags.host, "host", false, "Host a multiplayer session")
	runCmd.Flags().StringVar(&runFlags.join, "join", "", "Join the multiplayer session at host:port")
	runCmd.Flags().IntVar(&runFlags.peers, "peers", 0, "Run a whole multiplayer session here: N games (and a server)")
	runCmd.Flags().IntVar(&runFlags.port, "port", 0, "The session's UDP port")
}

// runGame launches the engine on a build folder or a .jm archive.
func runGame(target string) error { return runWith(target, runOptions{}) }

// gameToRun is what a run needs to know about its target.
type gameToRun struct {
	target, kind, manifestPath string
	man                        manifest.GameManifest
}

func loadRunTarget(target string) (gameToRun, error) {
	g := gameToRun{target: target, kind: "build", manifestPath: filepath.Join(target, archive.ManifestEntryKey)}
	var err error
	// A build is a folder; anything else is an archive (whatever its name).
	if info, statErr := os.Stat(target); statErr == nil && !info.IsDir() {
		g.manifestPath, g.kind = target, "archive"
		g.man, err = readArchiveManifest(target)
	} else {
		g.man, err = manifest.LoadManifest(g.manifestPath)
	}
	if err != nil {
		return g, fmt.Errorf("failed to load manifest from %s: %w", target, err)
	}
	return g, nil
}

func runWith(target string, opts runOptions) error {
	g, err := loadRunTarget(target)
	if err != nil {
		return err
	}
	if opts.peers > 0 {
		return runSession(g, opts)
	}
	env := os.Environ()
	if opts.port > 0 {
		env = append(env, fmt.Sprintf("JM_NET_PORT=%d", opts.port))
	}
	var exe string
	switch {
	case opts.server:
		if exe, err = resolveServerPath(g.man.EnginePath, g.manifestPath); err != nil {
			return err
		}
	default:
		if exe, err = resolveEnginePath(g.man.EnginePath, g.manifestPath); err != nil {
			return fmt.Errorf("engine binary not found: %w", err)
		}
		if opts.host {
			env = append(env, "JM_NET_HOST=1")
		} else if opts.join != "" {
			env = append(env, "JM_NET_JOIN="+opts.join)
		}
	}
	fmt.Printf("Running engine: %s with %s: %s\n", exe, g.kind, target)
	engineCmd := exec.Command(exe, target)
	engineCmd.Env = env
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
	bases := []string{buildDir, filepath.Dir(buildDir), "."}
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

// resolveServerPath finds journeyman_server: beside the game's engine (a
// release and a build put them together), else beside jm or on $PATH.
func resolveServerPath(enginePath, manifestPath string) (string, error) {
	name := "journeyman_server"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	if engine, err := resolveEnginePath(enginePath, manifestPath); err == nil {
		if candidate := filepath.Join(filepath.Dir(engine), name); isFile(candidate) {
			return candidate, nil
		}
	}
	if found, err := resolveEnginePath("journeyman_server", manifestPath); err == nil {
		return found, nil
	}
	return "", fmt.Errorf("journeyman_server not found beside the engine, beside jm or on $PATH " +
		"(a release ships it next to jm; from source, build the journeyman_server target)")
}

// executablePath is os.Executable, replaceable in tests.
var executablePath = os.Executable

func isFile(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}
