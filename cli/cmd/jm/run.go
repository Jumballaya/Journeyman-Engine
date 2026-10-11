package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"time"

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
                        (step [n], state [part...] [tag=Name], get <path>, until <path> <op> <value>, near tag=Name, press <Key>, quit)
  JM_RENDERER=none      no window or GL: runs with no display (a container)
  JM_HEADLESS=1         a hidden window, with GL: frames can be captured
  JM_STRICT=1           the first error ends the run with exit code 1
  JM_EXIT_AFTER_FRAMES=n, JM_CAPTURE_DIR + JM_CAPTURE_FRAMES, JM_DUMP_DIR,
  JM_INPUT_REPLAY, JM_ERRORS, JM_SEED ...: jm docs testing has them all.
A windowed run with JM_EXIT_AFTER_FRAMES (not driven) still going 30 s past
three times its frames' time is stopped: a sandbox blocking the window server
hangs it.

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
	watch        bool
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
	runCmd.Flags().BoolVar(&runFlags.watch, "watch", false, "Rebuild when the project's files change; the game reloads changed images, atlases, shaders, sounds and scripts as it runs")
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
	windowed := !opts.server && os.Getenv("JM_RENDERER") != "none" // the server has no window
	if windowed {
		if err := windowsBlocked(); err != nil {
			return err
		}
	}
	if opts.peers > 0 {
		if opts.watch {
			return fmt.Errorf("--watch runs one game; it doesn't work with --peers yet")
		}
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
	// A windowed run that ends itself gets a deadline: one that can't reach
	// the window server (a sandbox) would otherwise hang, not fail.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	frames, _ := strconv.Atoi(os.Getenv("JM_EXIT_AFTER_FRAMES"))
	frameTime := 1.0 / 60
	if dt, err := strconv.ParseFloat(os.Getenv("JM_FIXED_DT"), 64); err == nil && dt > 0 {
		frameTime = dt
	}
	limit := selfEndingGrace + time.Duration(3*float64(frames)*frameTime*float64(time.Second))
	if windowed && frames > 0 && !envOn("JM_DRIVE") {
		ctx, cancel = context.WithTimeout(ctx, limit)
		defer cancel()
	}
	// stderr: stdout is the game's, e.g. the driver's JSON lines (JM_DRIVE).
	fmt.Fprintf(os.Stderr, "Running engine: %s with %s: %s\n", exe, g.kind, target)
	engineCmd := exec.CommandContext(ctx, exe, target)
	engineCmd.Stdin = os.Stdin
	engineCmd.Stdout = os.Stdout
	engineCmd.Stderr = os.Stderr
	if opts.watch {
		root, ok := projectOfBuild(g)
		if !ok {
			return fmt.Errorf("--watch runs a project's build (its build folder), not %s", target)
		}
		env = append(env, "JM_WATCH=1")
		ctx, stop := context.WithCancel(context.Background())
		defer stop()
		go watchSources(ctx, root)
	}
	engineCmd.Env = env
	err = engineCmd.Run()
	if errors.Is(ctx.Err(), context.DeadlineExceeded) {
		return fmt.Errorf("the game hadn't run its %d frames after %v, so jm stopped it. If it printed hiservices-xpcservice, "+
			"a sandbox blocks the window server: run it again with escalated permissions (JM_RENDERER=none runs without one)", frames, limit)
	}
	return err
}

// envOn says whether a JM_* switch is set as the engine reads it ("0" is off).
func envOn(name string) bool { v := os.Getenv(name); return v != "" && v != "0" }

// selfEndingGrace is how long past its frames a self-ending run may take to start.
var selfEndingGrace = 30 * time.Second

// windowsBlocked says when this process can't open windows or OpenGL: under
// Codex's macOS sandbox, which makes the engine and editor hang or abort.
func windowsBlocked() error {
	if runtime.GOOS == "darwin" && os.Getenv("CODEX_SANDBOX") == "seatbelt" {
		return errors.New("Codex's sandbox blocks windows and OpenGL on macOS: run this again with escalated permissions " +
			"(ask the person to approve it). JM_RENDERER=none jm run runs the game's logic inside the sandbox")
	}
	return nil
}

// recordingProject says whether to record this run as a play, and in which
// project: a person playing a project's own build (root/build: what its
// replays run). Not runs a tool drives, replays or runs headless, nor
// archives, other builds or multiplayer peers.
func recordingProject(g gameToRun, opts runOptions) (string, bool) {
	// A watched run reloads files as it goes: no replay could follow it.
	if opts.noRecord || opts.watch || g.kind != "build" || opts.host || opts.join != "" {
		return "", false
	}
	for _, v := range []string{"JM_DRIVE", "JM_HEADLESS", "JM_INPUT_REPLAY", "JM_PLAY_SESSION", "JM_RECORD_DIR", "JM_EXIT_AFTER_FRAMES"} {
		if envOn(v) {
			return "", false
		}
	}
	if os.Getenv("JM_RENDERER") == "none" {
		return "", false
	}
	return projectOfBuild(g)
}

// projectOfBuild is the project whose own build (root/build) g is.
func projectOfBuild(g gameToRun) (string, bool) {
	build, err := filepath.Abs(g.target)
	if err != nil || g.kind != "build" {
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
