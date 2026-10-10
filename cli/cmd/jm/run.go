package main

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
	"github.com/spf13/cobra"
)

var runFlags runOptions

var runCmd = &cobra.Command{
	Use:   "run [build path or .jm archive]",
	Short: "Run the Journeyman game engine (default: ./build)",
	Long: `Runs the game (default: ./build).

When you play a project's build, the play is recorded in .jm/plays (F8 marks
a moment): jm plays shows them, and your agent can replay them exactly.
--no-record skips it; driven, replayed and headless runs never record.

Multiplayer (.jm.json "net"; see docs/networking.md):
  --server       run the dedicated server (journeyman_server) instead of the game
  --host         the game hosts a session; --join host:port joins one
  --peers N      a whole session on this machine: with net.topology "server",
                 a server and N games joining it; with "p2p", N games, the
                 first hosting. Each game gets its own save folder, and
                 output folders and files named in JM_CAPTURE_DIR, JM_DUMP_DIR,
                 JM_NET_TRACE and JM_ERRORS get a per-peer suffix (peer1...).
                 JM_INPUT_REPLAY may say {peer}: replay.{peer}.txt.
  --port P       the session's UDP port (default: net.port, else 7777)
  --latency MS   simulated network trouble: each message held MS milliseconds,
  --loss P       and unreliable ones (positions) dropped with probability P (0..1)

The engine's JM_* variables pass through; the ones for unattended runs:
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
		if err := runWith(target, runFlags); err != nil {
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

type runOptions struct {
	noRecord     bool
	server, host bool
	join         string
	peers, port  int
	latency      int
	loss         float64
}

// netEnv is what the run's network options set for the engines it starts.
func (o runOptions) netEnv() []string {
	var env []string
	if o.latency > 0 {
		env = append(env, fmt.Sprintf("JM_NET_LATENCY=%d", o.latency))
	}
	if o.loss > 0 {
		env = append(env, fmt.Sprintf("JM_NET_LOSS=%g", o.loss))
	}
	return env
}

func init() {
	runCmd.Flags().BoolVar(&runFlags.noRecord, "no-record", false, "Don't record this play (jm plays)")
	runCmd.Flags().BoolVar(&runFlags.server, "server", false, "Run the dedicated multiplayer server")
	runCmd.Flags().BoolVar(&runFlags.host, "host", false, "Host a multiplayer session")
	runCmd.Flags().StringVar(&runFlags.join, "join", "", "Join the multiplayer session at host:port")
	runCmd.Flags().IntVar(&runFlags.peers, "peers", 0, "Run a whole multiplayer session here: N games (and a server)")
	runCmd.Flags().IntVar(&runFlags.port, "port", 0, "The session's UDP port")
	runCmd.Flags().IntVar(&runFlags.latency, "latency", 0, "Simulated latency for every message, in milliseconds")
	runCmd.Flags().Float64Var(&runFlags.loss, "loss", 0, "Simulated loss of unreliable messages, 0..1")
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
	env := append(os.Environ(), opts.netEnv()...)
	if opts.port > 0 {
		env = append(env, fmt.Sprintf("JM_NET_PORT=%d", opts.port))
	}
	var exe string
	switch {
	case opts.server:
		if exe, err = resolveServerPath(); err != nil {
			return err
		}
	default:
		if exe, err = resolveEnginePath(); err != nil {
			return fmt.Errorf("engine binary not found: %w", err)
		}
		if opts.host {
			env = append(env, "JM_NET_HOST=1")
		} else if opts.join != "" {
			env = append(env, "JM_NET_JOIN="+opts.join)
		}
		if root, ok := recordingProject(g, opts); ok {
			_, _ = plays.Prune(root, keptPlays, true)
			dir, err := plays.Create(root, plays.ReadBuild(filepath.Join(root, "build")), version)
			if err != nil {
				return err
			}
			env = append(env, "JM_RECORD_DIR="+dir)
			fmt.Fprintf(os.Stderr, "Recording this play as %s (F8 marks a moment; jm plays show %s)\n",
				filepath.Base(dir), filepath.Base(dir))
		}
	}
	// stderr: stdout is the game's, e.g. the driver's JSON lines (JM_DRIVE).
	fmt.Fprintf(os.Stderr, "Running engine: %s with %s: %s\n", exe, g.kind, target)
	engineCmd := exec.Command(exe, target)
	engineCmd.Env = env
	engineCmd.Stdin = os.Stdin
	engineCmd.Stdout = os.Stdout
	engineCmd.Stderr = os.Stderr
	return engineCmd.Run()
}

// recordingProject says whether to record this run as a play, and in which
// project: a person playing a project's own build (root/build: what its
// replays run). Not runs a tool drives, replays or runs headless, nor
// archives, other builds or multiplayer peers.
func recordingProject(g gameToRun, opts runOptions) (string, bool) {
	if opts.noRecord || g.kind != "build" || opts.host || opts.join != "" {
		return "", false
	}
	for _, v := range []string{"JM_DRIVE", "JM_HEADLESS", "JM_INPUT_REPLAY", "JM_PLAY_SESSION", "JM_RECORD_DIR", "JM_EXIT_AFTER_FRAMES"} {
		if os.Getenv(v) != "" && os.Getenv(v) != "0" {
			return "", false
		}
	}
	if os.Getenv("JM_RENDERER") == "none" {
		return "", false
	}
	build, err := filepath.Abs(g.target)
	if err != nil {
		return "", false
	}
	root := filepath.Dir(build)
	if filepath.Base(build) != "build" || !isFile(filepath.Join(root, archive.ManifestEntryKey)) {
		return "", false
	}
	return root, true
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

// resolveEnginePath finds the engine. Games don't say where it is: it's the
// machine's, found by findBinary.
func resolveEnginePath() (string, error) {
	return findBinary("journeyman_engine", "JM_ENGINE")
}

// resolveServerPath finds journeyman_server: $JM_SERVER, else beside the
// engine (a release and a build put them together), else beside jm or on PATH.
func resolveServerPath() (string, error) {
	if os.Getenv("JM_SERVER") == "" {
		if engine, err := resolveEnginePath(); err == nil {
			if candidate := filepath.Join(filepath.Dir(engine), exeName("journeyman_server")); isFile(candidate) {
				return candidate, nil
			}
		}
	}
	return findBinary("journeyman_server", "JM_SERVER")
}

// findBinary finds one of the engine's programs (name): the path in the
// environment variable env if it's set (it must exist), else beside jm, where
// a release, and scripts/build-release.sh's build/bin, put the programs jm
// was built with, else on $PATH.
func findBinary(name, env string) (string, error) {
	if path := os.Getenv(env); path != "" {
		if isFile(path) {
			return filepath.Abs(path)
		}
		return "", fmt.Errorf("%s is %s, which isn't a file", env, path)
	}
	besideJM := ""
	if self, err := executablePath(); err == nil {
		besideJM = filepath.Dir(self)
		if candidate := filepath.Join(besideJM, exeName(name)); isFile(candidate) {
			return candidate, nil
		}
	}
	if found, err := exec.LookPath(name); err == nil {
		return found, nil
	}
	return "", fmt.Errorf("%s not found beside jm (%s) or on PATH: a release keeps it next to jm; from source, "+
		"scripts/build-release.sh puts jm and it in build/bin; or set %s to its path", name, besideJM, env)
}

// exeName is name as an executable's file name here (name.exe on Windows).
func exeName(name string) string {
	if runtime.GOOS == "windows" {
		return name + ".exe"
	}
	return name
}

// executablePath is os.Executable, replaceable in tests.
var executablePath = os.Executable

func isFile(path string) bool {
	info, err := os.Stat(path)
	return err == nil && !info.IsDir()
}
