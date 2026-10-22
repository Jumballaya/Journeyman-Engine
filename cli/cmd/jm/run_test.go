package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

func TestArchiveManifestRejectsDamagedEntries(t *testing.T) {
	for _, tc := range []struct{ entry, original, damaged string }{
		{archive.ManifestEntryKey, "Original", "Tampered"},
		{"asset.txt", "original", "tampered"},
	} {
		t.Run(tc.entry, func(t *testing.T) {
			var buf bytes.Buffer
			entries := []archive.AssetEntry{
				{SourcePath: archive.ManifestEntryKey, Type: "manifest", Payload: []byte(`{"name":"Original","entryScene":"s","scenes":["s"]}`)},
				{SourcePath: "asset.txt", Type: "data", Payload: []byte("original")},
			}
			if err := archive.WriteArchive(&buf, entries); err != nil {
				t.Fatal(err)
			}
			data := bytes.Replace(buf.Bytes(), []byte(tc.original), []byte(tc.damaged), 1)
			path := filepath.Join(t.TempDir(), "damaged.jm")
			if err := os.WriteFile(path, data, 0o644); err != nil {
				t.Fatal(err)
			}
			if _, err := readArchiveManifest(path); err == nil || !strings.Contains(err.Error(), "damaged") || !strings.Contains(err.Error(), tc.entry) {
				t.Fatalf("expected damage error naming %s, got %v", tc.entry, err)
			}
		})
	}
}

func TestRunGameArchiveExtractsManifestAndResolvesEngine(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("relies on /usr/bin/true; not present on Windows")
	}
	if _, err := os.Stat("/usr/bin/true"); err != nil {
		t.Skip("/usr/bin/true not present on this host")
	}

	tmp := t.TempDir()
	t.Setenv("JM_ENGINE", "/usr/bin/true")
	man := manifest.GameManifest{
		Name:       "RunArchiveTest",
		Version:    "0",
		EntryScene: "scenes/dummy.scene.json",
		Scenes:     []string{"scenes/dummy.scene.json"},
		Assets:     []string{},
	}
	manData, _ := json.Marshal(man)

	// An archive is any file, whatever it's called; a build is a folder.
	for _, name := range []string{"test.jm", "renamed.pak"} {
		archivePath := filepath.Join(tmp, name)
		f, err := os.Create(archivePath)
		if err != nil {
			t.Fatalf("create: %v", err)
		}
		entries := []archive.AssetEntry{
			{SourcePath: ".jm.json", Type: "manifest", Payload: manData},
		}
		if err := archive.WriteArchive(f, entries); err != nil {
			t.Fatalf("WriteArchive: %v", err)
		}
		f.Close()

		if err := runGame(archivePath); err != nil {
			t.Fatalf("runGame(%s): %v", name, err)
		}
	}
}

// fakeBuild is a build folder with a manifest.
func fakeBuild(t *testing.T) string {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, archive.ManifestEntryKey), []byte(`{"name":"x","entryScene":"s","scenes":["s"]}`), 0o644)
	return dir
}

// A run that should end itself but hangs (no window server) is stopped and says why.
func TestASelfEndingRunThatHangsIsStopped(t *testing.T) {
	skipOnWindows(t)
	t.Setenv("JM_ENGINE", fakeProgram(t, t.TempDir(), "program", "sleep 5"))
	t.Setenv("JM_EXIT_AFTER_FRAMES", "1")
	t.Setenv("CODEX_SANDBOX", "")
	t.Setenv("JM_RENDERER", "")
	t.Setenv("JM_DRIVE", "0") // off, as the engine reads it
	saved := selfEndingGrace
	selfEndingGrace = 200 * time.Millisecond
	defer func() { selfEndingGrace = saved }()
	start := time.Now()
	err := runWith(fakeBuild(t), runOptions{noRecord: true})
	if err == nil || !strings.Contains(err.Error(), "hadn't run its 1 frames") || time.Since(start) > 3*time.Second {
		t.Errorf("after %v: %v", time.Since(start), err)
	}
	start = time.Now() // and profiled
	err = runWith(fakeBuild(t), runOptions{noRecord: true, profile: true})
	if err == nil || !strings.Contains(err.Error(), "hadn't run its 1 frames") || time.Since(start) > 3*time.Second {
		t.Errorf("profiled, after %v: %v", time.Since(start), err)
	}
}

// A fixed simulation step doesn't speed up a windowed run's frames, so it doesn't shorten the deadline.
func TestAFixedStepDoesntShortenTheDeadline(t *testing.T) {
	skipOnWindows(t)
	t.Setenv("JM_ENGINE", fakeProgram(t, t.TempDir(), "program", "sleep 1"))
	t.Setenv("JM_EXIT_AFTER_FRAMES", "100") // 10 s at 0.1 s a frame; 0.3 s at 3x dt
	t.Setenv("JM_FIXED_DT", "0.001")
	t.Setenv("CODEX_SANDBOX", "")
	t.Setenv("JM_RENDERER", "")
	t.Setenv("JM_DRIVE", "")
	saved := selfEndingGrace
	selfEndingGrace = 200 * time.Millisecond
	defer func() { selfEndingGrace = saved }()
	if err := runWith(fakeBuild(t), runOptions{noRecord: true}); err != nil {
		t.Errorf("a healthy run was stopped: %v", err)
	}
	// JM_REALTIME paces frames at a long dt; a huge count can't overflow into no time at all.
	t.Setenv("JM_EXIT_AFTER_FRAMES", "6") // 9 s at 3x dt; 0.6 s at 0.1 s a frame
	t.Setenv("JM_FIXED_DT", "0.5")
	if err := runWith(fakeBuild(t), runOptions{noRecord: true}); err != nil {
		t.Errorf("a realtime run was stopped: %v", err)
	}
	t.Setenv("JM_EXIT_AFTER_FRAMES", "100000000000")
	if err := runWith(fakeBuild(t), runOptions{noRecord: true}); err != nil {
		t.Errorf("a long run was stopped: %v", err)
	}
}

func TestCodexsMacSandboxIsNamedBeforeAWindowHangs(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("macOS only")
	}
	t.Setenv("JM_ENGINE", fakeProgram(t, t.TempDir(), "program", "exit 0"))
	t.Setenv("CODEX_SANDBOX", "seatbelt")
	t.Setenv("JM_RENDERER", "")
	if err := runWith(fakeBuild(t), runOptions{noRecord: true}); err == nil || !strings.Contains(err.Error(), "escalated permissions") {
		t.Errorf("windowed: %v", err)
	}
	if err := runWith(fakeBuild(t), runOptions{noRecord: true, server: true, peers: 1}); err == nil || !strings.Contains(err.Error(), "escalated permissions") {
		t.Errorf("a server with peers opens their windows: %v", err)
	}
	t.Setenv("JM_RENDERER", "none")
	if err := runWith(fakeBuild(t), runOptions{noRecord: true}); err != nil {
		t.Errorf("JM_RENDERER=none runs in the sandbox: %v", err)
	}
}

// An engine whose cwd moves on start (GLFW in an .app goes to Contents/Resources)
// still finds a build named relative to where jm ran.
func TestAnEngineThatChangesDirectoryStillFindsTheBuild(t *testing.T) {
	skipOnWindows(t)
	t.Setenv("JM_ENGINE", fakeProgram(t, t.TempDir(), "journeyman_engine", `cd / && test -f "$1/.jm.json"`))
	t.Setenv("JM_RENDERER", "none")
	build := fakeBuild(t)
	t.Chdir(filepath.Dir(build))
	if err := runWith(filepath.Base(build), runOptions{noRecord: true}); err != nil {
		t.Errorf("relative build: %v", err)
	}
}

// --fullscreen and --windowed reach the engine as JM_FULLSCREEN, over the game's config.
func TestRunOpensFullscreenOrWindowedAsAsked(t *testing.T) {
	skipOnWindows(t)
	t.Setenv("JM_RENDERER", "none")
	for _, c := range []struct {
		opts runOptions
		want string
	}{{runOptions{fullscreen: true}, "1"}, {runOptions{windowed: true}, "0"}, {runOptions{}, ""}} {
		c.opts.noRecord = true
		t.Setenv("JM_ENGINE", fakeProgram(t, t.TempDir(), "program", `test "$JM_FULLSCREEN" = "`+c.want+`"`))
		if err := runWith(fakeBuild(t), c.opts); err != nil {
			t.Errorf("%+v: JM_FULLSCREEN isn't %q: %v", c.opts, c.want, err)
		}
	}
	if err := runWith(fakeBuild(t), runOptions{fullscreen: true, windowed: true}); err == nil {
		t.Error("both at once")
	}
	if err := runWith(fakeBuild(t), runOptions{fullscreen: true, peers: 2}); err == nil {
		t.Error("fullscreen peers")
	}
}

// Whichever way a person comes to play (jm run, or resuming a play), the run
// is recorded and play_live can steer it.
func TestAPersonsPlayIsRecordedAndSteerable(t *testing.T) {
	skipOnWindows(t)
	root := t.TempDir()
	build := filepath.Join(root, "build")
	os.MkdirAll(build, 0o755)
	manifestJSON := `{"name":"x","entryScene":"s","scenes":["s"]}`
	os.WriteFile(filepath.Join(root, archive.ManifestEntryKey), []byte(manifestJSON), 0o644)
	os.WriteFile(filepath.Join(build, archive.ManifestEntryKey), []byte(manifestJSON), 0o644)
	envFile := filepath.Join(t.TempDir(), "env")
	t.Setenv("JM_ENGINE", fakeProgram(t, t.TempDir(), "engine", "env > "+envFile))
	for _, v := range []string{"JM_EXIT_AFTER_FRAMES", "JM_DRIVE", "JM_RECORD_DIR", "JM_CONTROL", "CODEX_SANDBOX"} {
		t.Setenv(v, "")
	}
	launched := func(how string) {
		t.Helper()
		data, _ := os.ReadFile(envFile)
		env := string(data)
		if !strings.Contains(env, "JM_RECORD_DIR="+plays.Root(evalOrSelf(root))) && !strings.Contains(env, "JM_RECORD_DIR="+plays.Root(root)) {
			t.Errorf("%s isn't recorded: %s", how, env)
		}
		if !strings.Contains(env, "JM_CONTROL="+liveDir(root)) && !strings.Contains(env, "JM_CONTROL="+liveDir(evalOrSelf(root))) {
			t.Errorf("%s can't be steered by play_live", how)
		}
		if !strings.Contains(env, "\nPWD="+root+"\n") && !strings.Contains(env, "\nPWD="+evalOrSelf(root)+"\n") {
			t.Errorf("%s doesn't start in the game (its log goes where the next build deletes it)", how)
		}
		os.Remove(envFile)
	}
	t.Chdir(root)
	if err := runWith(build, runOptions{}); err != nil {
		t.Fatal(err)
	}
	launched("jm run")
	p, err := plays.Create(root, plays.ReadBuild(build), version)
	if err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(p, "session.json"), []byte(`{"frames": 60, "seconds": 1, "started": "2000-01-01T00:00:00Z"}`), 0o644) // a second of play
	os.WriteFile(filepath.Join(p, "frames.bin"), make([]byte, 4*60), 0o644)                                                          // its frames' times (dt 0)
	for i := range keptPlays {                                                                                                       // newer plays: the resumed one is the oldest, first to be pruned
		later := filepath.Join(plays.Root(root), fmt.Sprintf("2999-01-01_%06d", i))
		os.MkdirAll(later, 0o755)
		os.WriteFile(filepath.Join(later, "session.json"), []byte(`{"format":1,"frames":1,"markers":[],"started":"2999-01-01T00:00:00Z"}`), 0o644)
	}
	if err := resumePlay(root, filepath.Base(p), "0"); err != nil {
		t.Fatal(err)
	}
	launched("a resumed play")
	if _, err := os.Stat(filepath.Join(p, "frames.bin")); err != nil {
		t.Error("the play a resume replays went with older plays:", err)
	}
}
