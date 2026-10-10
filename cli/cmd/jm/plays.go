package main

import (
	"bufio"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"

	"github.com/spf13/cobra"
)

var playsCmd = &cobra.Command{
	Use:   "plays",
	Short: "The plays `jm run` recorded: list, look at, replay and resume them",
	Long: `Every time you play with jm run, the play is recorded in .jm/plays/<id>:
your inputs and frame timing (enough to replay it exactly), the state every
half second, a thumbnail every second, and the moments you marked with F8.
Your agent can then see what you saw, at the moment you mean.

A play is named by its id, a unique start of it, "latest" (the default),
or "-1", "-2" for the ones before. A moment in it is a frame ("420"), a time
("12.5s", "1:05"), a marker ("marker:2" or "m2"), "start" or "end".

  jm plays                         the plays, newest first
  jm plays show [play]             what happened: scenes, values over time, markers
  jm plays state [play] [moment]   the game's state then: all but the draw list,
                                   or the driver's parts (session, ui, draw, tag=Name, ...)
  jm plays frame [play] [moment]   an image of that moment
  jm plays drive [play] [moment]   the driver (JM_DRIVE), starting at that moment
  jm plays resume [play] [moment]  play on from that moment yourself
  jm plays verify [play]           does it still replay the same (after a change)?
  jm plays prune --keep N          delete all but the newest N

jm run keeps the newest 40 plays, and every play with a marker.

Replays need the build the play was made with: after changing the game, a
replay may go differently (that's verify's question). --json everywhere.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error { return listPlays(cmd.OutOrStdout()) },
}

var playsPruneKeep int

func init() {
	playsCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	sub := func(use, short string, maxArgs int, run func(cmd *cobra.Command, args []string) error) *cobra.Command {
		c := &cobra.Command{Use: use, Short: short, Args: cobra.MaximumNArgs(maxArgs), RunE: run}
		playsCmd.AddCommand(c)
		return c
	}
	sub("list", "The recorded plays, newest first", 0, func(cmd *cobra.Command, _ []string) error {
		return listPlays(cmd.OutOrStdout())
	})
	sub("show [play]", "What happened in a play", 1, func(cmd *cobra.Command, args []string) error {
		return showPlay(cmd.OutOrStdout(), arg(args, 0))
	})
	sub("state [play] [moment] [part...]", "The game's state at a moment (replayed)", 64, func(cmd *cobra.Command, args []string) error {
		return playState(cmd.OutOrStdout(), arg(args, 0), arg(args, 1), args[min(len(args), 2):])
	})
	frame := sub("frame [play] [moment]", "An image of a moment (replayed with GL, else the nearest thumbnail)", 2,
		func(cmd *cobra.Command, args []string) error {
			out, _ := cmd.Flags().GetString("out")
			return playFrame(cmd.OutOrStdout(), arg(args, 0), arg(args, 1), out)
		})
	frame.Flags().String("out", "", "where to write the PNG (default: the play's folder)")
	sub("drive [play] [moment]", "The stepped driver, starting at a moment of the play", 2, func(cmd *cobra.Command, args []string) error {
		return drivePlay(arg(args, 0), arg(args, 1))
	})
	sub("resume [play] [moment]", "Play on yourself from a moment (recorded as a new play)", 2, func(cmd *cobra.Command, args []string) error {
		return resumePlay(arg(args, 0), arg(args, 1))
	})
	sub("verify [play]", "Replay a play to its end: does it go the same way?", 1, func(cmd *cobra.Command, args []string) error {
		return verifyPlay(cmd.OutOrStdout(), arg(args, 0))
	})
	prune := sub("prune", "Delete all but the newest plays", 0, func(cmd *cobra.Command, _ []string) error {
		return prunePlays(cmd.OutOrStdout(), playsPruneKeep)
	})
	prune.Flags().IntVar(&playsPruneKeep, "keep", 20, "how many to keep")
}

func arg(args []string, i int) string {
	if i < len(args) {
		return args[i]
	}
	return ""
}

func projectRoot() (string, error) {
	root, err := os.Getwd()
	if err != nil {
		return "", err
	}
	if _, err := os.Stat(filepath.Join(root, archive.ManifestEntryKey)); err != nil {
		return "", fmt.Errorf("not in a project (no %s here)", archive.ManifestEntryKey)
	}
	return root, nil
}

func writeJSON(w io.Writer, v any) error {
	out, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		return err
	}
	_, err = fmt.Fprintln(w, string(out))
	return err
}

// playListing is a play as `jm plays` lists it.
type playListing struct {
	ID      string  `json:"id"`
	Started string  `json:"started"`
	Seconds float64 `json:"seconds"`
	Frames  uint64  `json:"frames"`
	Markers int     `json:"markers"`
	Ended   string  `json:"ended"`
	Stale   bool    `json:"stale,omitempty"` // made with another build than the current one
}

func listPlays(w io.Writer) error {
	root, err := projectRoot()
	if err != nil {
		return err
	}
	all, err := plays.List(root)
	if err != nil {
		return err
	}
	current := buildFingerprint(filepath.Join(root, "build"))
	listing := []playListing{}
	for _, p := range all {
		listing = append(listing, playListing{p.ID, p.Meta.Started, p.Meta.Seconds, p.Meta.Frames, len(p.Meta.Markers),
			p.Meta.Ended, gameChangedSince(root, p, current)})
	}
	if jsonOutput {
		return writeJSON(w, listing)
	}
	if len(listing) == 0 {
		fmt.Fprintln(w, "No plays recorded yet: `jm run` records each time you play (F8 marks a moment).")
		return nil
	}
	for _, l := range listing {
		note := ""
		if l.Markers > 0 {
			note = fmt.Sprintf(", %d marker(s)", l.Markers)
		}
		if l.Ended != "quit" {
			note += ", " + l.Ended
		}
		if l.Stale {
			note += ", older build"
		}
		fmt.Fprintf(w, "%s  %s%s\n", l.ID, clock(l.Seconds), note)
	}
	return nil
}

// valueLine is a session value over the play: "score: 0 → 100", with where
// it started (when the game set it after the play began) and its range when
// that says more than its ends.
func valueLine(s plays.Summary, v plays.Series) string {
	line := fmt.Sprintf("%s: %g → %g", v.Key, v.First, v.Last)
	if v.Since > 0 && v.Since < len(s.SampleT) {
		line = fmt.Sprintf("%s: %g (set at %s) → %g", v.Key, v.First, clock(s.SampleT[v.Since]), v.Last)
		if v.Min == v.Max {
			line = fmt.Sprintf("%s: %g (set at %s)", v.Key, v.First, clock(s.SampleT[v.Since]))
		}
	}
	if v.Min < min(v.First, v.Last) || v.Max > max(v.First, v.Last) {
		line += fmt.Sprintf(" (min %g, max %g)", v.Min, v.Max)
	}
	return line
}

// endedText is how a play ended, for a person or a model to read.
func endedText(ended string) string {
	if ended == "running" {
		return "didn't end (still running, or the game crashed)"
	}
	return ended
}

func clock(seconds float64) string {
	m := int(seconds) / 60
	return fmt.Sprintf("%d:%04.1f", m, seconds-float64(m*60))
}

func findPlay(ref string) (string, *plays.Play, error) {
	root, err := projectRoot()
	if err != nil {
		return "", nil, err
	}
	p, err := plays.Find(root, ref)
	return root, p, err
}

func showPlay(w io.Writer, ref string) error {
	root, p, err := findPlay(ref)
	if err != nil {
		return err
	}
	s, err := p.Summarize()
	if err != nil {
		return err
	}
	stale := gameChangedSince(root, p, buildFingerprint(filepath.Join(root, "build")))
	if jsonOutput {
		return writeJSON(w, struct {
			plays.Summary
			Dir   string `json:"dir"`
			Stale bool   `json:"stale"`
		}{s, p.Dir, stale})
	}
	fmt.Fprintf(w, "%s: %s of %s (%d frames), %s\n", s.ID, clock(s.Seconds), s.Game, s.Frames, endedText(s.Ended))
	if stale {
		fmt.Fprintln(w, "(made with an older build: replays may go differently; jm plays verify says)")
	}
	for _, span := range s.Scenes {
		fmt.Fprintf(w, "  %s–%s  %s\n", clock(span.From), clock(span.To), span.Scene)
	}
	for _, v := range s.Values {
		fmt.Fprintf(w, "  %s\n", valueLine(s, v))
	}
	for _, m := range s.Markers {
		note := ""
		if m.Note != "" {
			note = ": " + m.Note
		}
		fmt.Fprintf(w, "  marker %d at %s (frame %d, %s)%s\n", m.N, clock(m.Time), m.Frame, m.Scene, note)
	}
	fmt.Fprintf(w, "  %d thumbnails in %s\n", len(s.Thumbs), filepath.Join(p.Dir, "thumbs"))
	return nil
}

// replayer runs the engine on the project's build replaying a play, stepped
// by the driver: to(frame) gets there, then commands ask about it.
type replayer struct {
	cmd    *exec.Cmd
	in     io.WriteCloser
	out    *bufio.Scanner
	frame  uint64
	errors []json.RawMessage // what the game logged on the way
}

// startReplay starts a replay of p cut at `until` frames (0: the whole play),
// with GL (a hidden window, for images) or without.
func startReplay(root string, p *plays.Play, until uint64, gl bool) (*replayer, error) {
	engine, err := resolveEnginePath()
	if err != nil {
		return nil, err
	}
	build := filepath.Join(root, "build")
	if _, err := os.Stat(filepath.Join(build, archive.ManifestEntryKey)); err != nil {
		return nil, fmt.Errorf("no build to replay with: jm build first")
	}
	cmd := exec.Command(engine, ".")
	cmd.Dir = build
	cmd.Env = append(os.Environ(), "JM_DRIVE=1", "JM_PLAY_SESSION="+p.Dir)
	if until > 0 {
		cmd.Env = append(cmd.Env, fmt.Sprintf("JM_PLAY_UNTIL=%d", until))
	}
	if gl {
		cmd.Env = append(cmd.Env, "JM_HEADLESS=1")
	} else {
		cmd.Env = append(cmd.Env, "JM_RENDERER=none")
	}
	r := &replayer{cmd: cmd}
	if r.in, err = cmd.StdinPipe(); err != nil {
		return nil, err
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	r.out = bufio.NewScanner(stdout)
	r.out.Buffer(make([]byte, 1<<20), 1<<28)
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	if _, err := r.read(); err != nil { // the ready line
		r.close()
		return nil, err
	}
	return r, nil
}

func (r *replayer) read() (map[string]json.RawMessage, error) {
	if !r.out.Scan() {
		_ = r.cmd.Wait()
		return nil, errors.New("the engine stopped (its log: build/logs/engine.log)")
	}
	var reply map[string]json.RawMessage
	if err := json.Unmarshal(r.out.Bytes(), &reply); err != nil {
		return nil, fmt.Errorf("the engine said %q", r.out.Text())
	}
	var errs []json.RawMessage
	if json.Unmarshal(reply["errors"], &errs) == nil {
		r.errors = append(r.errors, errs...)
	}
	return reply, nil
}

func (r *replayer) do(command string) (map[string]json.RawMessage, error) {
	if _, err := fmt.Fprintln(r.in, command); err != nil {
		return nil, err
	}
	reply, err := r.read()
	if err != nil {
		return nil, err
	}
	if string(reply["ok"]) != "true" {
		var msg string
		_ = json.Unmarshal(reply["error"], &msg)
		return reply, fmt.Errorf("%s: %s", command, msg)
	}
	return reply, nil
}

// to runs the replay through frame f: the state then is frame f's.
func (r *replayer) to(f uint64) error {
	if f+1 <= r.frame {
		return nil
	}
	_, err := r.do(fmt.Sprintf("step %d", f+1-r.frame))
	r.frame = f + 1
	return err
}

func (r *replayer) close() {
	_, _ = fmt.Fprintln(r.in, "quit")
	_ = r.in.Close()
	_ = r.cmd.Wait()
}

// momentOf resolves a play and a moment in it.
func momentOf(ref, at string) (string, *plays.Play, uint64, error) {
	root, p, err := findPlay(ref)
	if err != nil {
		return "", nil, 0, err
	}
	f, err := p.FrameAt(at)
	return root, p, f, err
}

func playState(w io.Writer, ref, at string, parts []string) error {
	root, p, f, err := momentOf(ref, at)
	if err != nil {
		return err
	}
	r, err := startReplay(root, p, f+1, false)
	if err != nil {
		return err
	}
	defer r.close()
	if err := r.to(f); err != nil {
		return err
	}
	if len(parts) == 0 {
		// Everything but the draw list (a frame's every quad): what the game
		// is, not how it's drawn; "draw" asks for it.
		parts = []string{"time", "scene", "entities", "session", "save", "ui", "replay"}
	}
	reply, err := r.do("state " + strings.Join(parts, " "))
	if err != nil {
		return err
	}
	var state any
	_ = json.Unmarshal(reply["state"], &state)
	return writeJSON(w, map[string]any{"play": p.ID, "frame": f, "state": state, "errors": nonNil(r.errors)})
}

func nonNil(v []json.RawMessage) []json.RawMessage {
	if v == nil {
		return []json.RawMessage{}
	}
	return v
}

// playImage writes an image of frame f of the play to out: replayed with GL
// when there's a display (or software GL), else the nearest thumbnail (a JPEG,
// smaller). It says which it made, and when the game has changed since the
// play (the replay is then this build's, not what the player saw).
func playImage(root string, p *plays.Play, f uint64, out string) (path, source string, err error) {
	build := buildFingerprint(filepath.Join(root, "build"))
	if out == "" {
		// Kept per build: after a change the same frame can look different.
		out = filepath.Join(p.Dir, "frames", build, fmt.Sprintf("%06d.png", f))
	}
	replayed := "replay"
	if gameChangedSince(root, p, build) {
		replayed = "replay with the current build (the game changed since this play: not what the player saw)"
	}
	if abs, err := filepath.Abs(out); err == nil {
		out = abs
	}
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		return "", "", err
	}
	if _, err := os.Stat(out); err == nil && build != "" && strings.HasPrefix(out, p.Dir) {
		return out, replayed, nil // made before, by this build
	}
	if !hasDisplay() {
		// fall through to the thumbnail
	} else if r, err := startReplay(root, p, f+1, true); err == nil {
		defer r.close()
		if r.to(f) == nil {
			if _, err := r.do("capture " + out); err == nil {
				return out, replayed, nil
			}
		}
	}
	times, _ := p.Times()
	thumbs := p.Thumbs(times)
	if len(thumbs) == 0 {
		return "", "", errors.New("no image: replaying needs a display or software GL (xvfb-run), and the play has no thumbnails")
	}
	best := thumbs[0]
	for _, t := range thumbs {
		if absDiff(t.Frame, f) < absDiff(best.Frame, f) {
			best = t
		}
	}
	return best.Path, fmt.Sprintf("thumbnail of frame %d", best.Frame), nil
}

func absDiff(a, b uint64) uint64 {
	if a > b {
		return a - b
	}
	return b - a
}

func playFrame(w io.Writer, ref, at, out string) error {
	root, p, f, err := momentOf(ref, at)
	if err != nil {
		return err
	}
	path, source, err := playImage(root, p, f, out)
	if err != nil {
		return err
	}
	if jsonOutput {
		return writeJSON(w, map[string]any{"play": p.ID, "frame": f, "path": path, "source": source})
	}
	fmt.Fprintln(w, path)
	return nil
}

func drivePlay(ref, at string) error {
	root, p, f, err := momentOf(ref, at)
	if err != nil {
		return err
	}
	r, err := startReplay(root, p, f+1, hasDisplay())
	if err != nil {
		return err
	}
	if err := r.to(f); err != nil {
		r.close()
		return err
	}
	// Hand over: the caller's commands go to the engine, its answers come back.
	fmt.Printf(`{"ok":true,"ready":true,"frame":%d,"play":%q}`+"\n", r.frame, p.ID)
	go func() {
		_, _ = io.Copy(r.in, os.Stdin)
		_ = r.in.Close()
	}()
	for r.out.Scan() {
		fmt.Println(r.out.Text())
	}
	return r.cmd.Wait()
}

// hasDisplay says whether GL can draw here (so captures work): macOS and
// Windows always can, Linux with an X or Wayland display, and nothing when
// JM_RENDERER=none says not to try.
func hasDisplay() bool {
	if os.Getenv("JM_RENDERER") == "none" {
		return false
	}
	return runtime.GOOS != "linux" || os.Getenv("DISPLAY") != "" || os.Getenv("WAYLAND_DISPLAY") != ""
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func resumePlay(ref, at string) error {
	root, p, f, err := momentOf(ref, at)
	if err != nil {
		return err
	}
	engine, err := resolveEnginePath()
	if err != nil {
		return err
	}
	record := newPlayDir(root)
	fmt.Fprintf(os.Stderr, "Resuming %s at frame %d (fast-forwarding there); recording as %s\n", p.ID, f, filepath.Base(record))
	cmd := exec.Command(engine, ".")
	cmd.Dir = filepath.Join(root, "build")
	cmd.Env = append(os.Environ(), "JM_PLAY_SESSION="+p.Dir, fmt.Sprintf("JM_PLAY_UNTIL=%d", f), "JM_PLAY_THEN=live",
		"JM_RECORD_DIR="+record)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	writePlayInfo(root, record)
	// The new play replays the old one up to there, without drawing it: its
	// thumbnails of that part are the old play's.
	times, _ := p.Times()
	for _, t := range p.Thumbs(times) {
		if t.Frame < f {
			if data, err := os.ReadFile(t.Path); err == nil {
				_ = os.MkdirAll(filepath.Join(record, "thumbs"), 0o755)
				_ = os.WriteFile(filepath.Join(record, "thumbs", filepath.Base(t.Path)), data, 0o644)
			}
		}
	}
	return cmd.Run()
}

func verifyPlay(w io.Writer, ref string) error {
	root, p, err := findPlay(ref)
	if err != nil {
		return err
	}
	last, err := p.FrameAt("end")
	if err != nil {
		return err
	}
	r, err := startReplay(root, p, 0, false)
	if err != nil {
		return err
	}
	defer r.close()
	if err := r.to(last); err != nil {
		return err
	}
	reply, err := r.do("state replay")
	if err != nil {
		return err
	}
	var state struct {
		Replay struct {
			Diverged *uint64 `json:"diverged"`
		} `json:"replay"`
	}
	_ = json.Unmarshal(reply["state"], &state)
	result := map[string]any{"play": p.ID, "frames": last + 1, "same": state.Replay.Diverged == nil,
		"errors": nonNil(r.errors)}
	if d := state.Replay.Diverged; d != nil {
		result["differsBy"] = *d
	}
	if jsonOutput {
		return writeJSON(w, result)
	}
	if state.Replay.Diverged == nil {
		fmt.Fprintf(w, "%s replays the same through its %d frames\n", p.ID, last+1)
	} else {
		fmt.Fprintf(w, "%s goes differently now: by frame %d (the build changed since it was played, or something in the game isn't deterministic)\n",
			p.ID, *state.Replay.Diverged)
	}
	if !jsonOutput && len(r.errors) > 0 {
		fmt.Fprintf(w, "%d error(s) on the way\n", len(r.errors))
	}
	return nil
}

func prunePlays(w io.Writer, keep int) error {
	root, err := projectRoot()
	if err != nil {
		return err
	}
	all, err := plays.List(root)
	if err != nil {
		return err
	}
	removed := 0
	for i, p := range all {
		if i < keep {
			continue
		}
		if err := os.RemoveAll(p.Dir); err != nil {
			return err
		}
		removed++
	}
	if jsonOutput {
		return writeJSON(w, map[string]int{"removed": removed, "kept": min(keep, len(all))})
	}
	fmt.Fprintf(w, "removed %d play(s), kept %d\n", removed, min(keep, len(all)))
	return nil
}

// keptPlays is how many unmarked plays jm run keeps: older ones go when a new
// one starts. A play with a marker is kept until pruned by hand: the person
// marked something in it.
const keptPlays = 40

// pruneOldPlays drops the oldest unmarked plays past keptPlays.
func pruneOldPlays(root string) {
	all, err := plays.List(root)
	if err != nil {
		return
	}
	unmarked := 0
	for _, p := range all {
		if len(p.Meta.Markers) > 0 {
			continue
		}
		if unmarked++; unmarked > keptPlays {
			_ = os.RemoveAll(p.Dir)
		}
	}
}

// newPlayDir names a new play's folder: when it started, sortable.
func newPlayDir(root string) string {
	base := filepath.Join(plays.Root(root), time.Now().Format("2006-01-02_150405"))
	dir := base
	for i := 2; fileExists(dir); i++ {
		dir = fmt.Sprintf("%s_%d", base, i)
	}
	return dir
}

// writePlayInfo notes, beside the engine's files, which build the play is of.
func writePlayInfo(root, dir string) {
	_ = os.MkdirAll(dir, 0o755)
	info, _ := json.Marshal(map[string]string{"build": buildFingerprint(filepath.Join(root, "build")), "jm": version})
	_ = os.WriteFile(filepath.Join(dir, "jm.json"), info, 0o644)
}

// gameChangedSince says whether the build differs from the one the play was
// made with: by fingerprint when jm recorded it. The editor's plays have none:
// while the build is older than the play it's the one the play was made with,
// so its fingerprint is pinned to the play then (and a later rebuild of the
// same game doesn't count as a change); a newer build is taken as changed.
func gameChangedSince(root string, p *plays.Play, current string) bool {
	if recorded := recordedBuild(p); recorded != "" {
		return recorded != current
	}
	info, err := os.Stat(filepath.Join(root, "build", archive.ManifestEntryKey))
	started, perr := time.Parse(time.RFC3339, p.Meta.Started)
	if err != nil || perr != nil {
		return false
	}
	if info.ModTime().After(started) {
		return true
	}
	if current != "" && p.Meta.Ended != "running" {
		pinned, _ := json.Marshal(map[string]string{"build": current, "jm": version, "pinned": "after the play, by jm"})
		_ = os.WriteFile(filepath.Join(p.Dir, "jm.json"), pinned, 0o644)
	}
	return false
}

func recordedBuild(p *plays.Play) string {
	data, err := os.ReadFile(filepath.Join(p.Dir, "jm.json"))
	if err != nil {
		return ""
	}
	var info struct {
		Build string `json:"build"`
	}
	_ = json.Unmarshal(data, &info)
	return info.Build
}

// buildFingerprint hashes what decides how a game plays (compiled scripts,
// scenes, prefabs, data, the manifest), not its images or sounds.
func buildFingerprint(build string) string {
	h := sha256.New()
	var files []string
	_ = filepath.WalkDir(build, func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return nil
		}
		switch filepath.Ext(path) {
		case ".ts", ".json", ".tmj", ".tsj":
			files = append(files, path)
		}
		return nil
	})
	if len(files) == 0 {
		return ""
	}
	sort.Strings(files)
	for _, f := range files {
		data, err := os.ReadFile(f)
		if err != nil {
			continue
		}
		rel, _ := filepath.Rel(build, f)
		fmt.Fprintf(h, "%s\x00%d\x00", filepath.ToSlash(rel), len(data))
		h.Write(data)
	}
	return hex.EncodeToString(h.Sum(nil))[:16]
}
