package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"

	"github.com/spf13/cobra"
)

var playsCmd = &cobra.Command{
	Use:   "plays",
	Short: "The plays `jm run` recorded: list, look at, replay and resume them",
	Long: `Every time you play with jm run, the play is recorded in .jm/plays/<id>:
your inputs and frame timing (enough to replay it exactly), the state every
30 frames, a thumbnail every 60, and the moments you marked with F8.
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
	RunE: func(cmd *cobra.Command, args []string) error { return emitPlays(cmd.OutOrStdout()) },
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
		return emitPlays(cmd.OutOrStdout())
	})
	sub("show [play]", "What happened in a play", 1, func(cmd *cobra.Command, args []string) error {
		_, p, b, err := openPlay(arg(args, 0))
		if err != nil {
			return err
		}
		o, err := overviewOf(p, b)
		return printResult(cmd.OutOrStdout(), o, err)
	})
	sub("state [play] [moment] [part...]", "The game's state at a moment (replayed)", 64, func(cmd *cobra.Command, args []string) error {
		root, p, _, f, err := openMoment(arg(args, 0), arg(args, 1))
		if err != nil {
			return err
		}
		s, err := stateAt(root, p, f, args[min(len(args), 2):])
		if err != nil {
			return err
		}
		return writeJSON(cmd.OutOrStdout(), s)
	})
	frame := sub("frame [play] [moment]", "An image of a moment (replayed with GL, else the nearest thumbnail)", 2,
		func(cmd *cobra.Command, args []string) error {
			root, p, b, f, err := openMoment(arg(args, 0), arg(args, 1))
			if err != nil {
				return err
			}
			out, _ := cmd.Flags().GetString("out")
			r, err := frameImage(root, p, b, f, out)
			return printResult(cmd.OutOrStdout(), r, err)
		})
	frame.Flags().String("out", "", "where to write the PNG (default: the play's folder)")
	sub("drive [play] [moment]", "The stepped driver, starting at a moment of the play", 2, func(cmd *cobra.Command, args []string) error {
		return drivePlay(arg(args, 0), arg(args, 1))
	})
	sub("resume [play] [moment]", "Play on yourself from a moment (recorded as a new play)", 2, func(cmd *cobra.Command, args []string) error {
		return resumePlay(arg(args, 0), arg(args, 1))
	})
	sub("verify [play]", "Replay a play to its end: does it go the same way?", 1, func(cmd *cobra.Command, args []string) error {
		root, p, b, err := openPlay(arg(args, 0))
		if err != nil {
			return err
		}
		v, err := verify(root, p, b)
		return printResult(cmd.OutOrStdout(), v, err)
	})
	prune := sub("prune", "Delete all but the newest plays (marked ones too)", 0, func(cmd *cobra.Command, _ []string) error {
		root, err := projectRoot()
		if err != nil {
			return err
		}
		if playsPruneKeep < 0 {
			return errors.New("--keep is how many to keep: 0 or more")
		}
		removed, err := plays.Prune(root, playsPruneKeep, false)
		return printResult(cmd.OutOrStdout(), pruned{removed}, err)
	})
	prune.Flags().IntVar(&playsPruneKeep, "keep", keptPlays, "how many to keep")
}

// keptPlays is how many unmarked plays jm keeps: older ones go when a new one
// starts. A marked play stays until pruned by hand: the person marked it.
const keptPlays = 40

// texter is a result a person can read as text; --json writes it as JSON.
type texter interface{ text() string }

func printResult(w io.Writer, v texter, err error) error {
	if err != nil {
		return err
	}
	if jsonOutput {
		return writeJSON(w, v)
	}
	_, err = fmt.Fprint(w, v.text())
	return err
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

func clock(seconds float64) string {
	m := int(seconds) / 60
	return fmt.Sprintf("%d:%04.1f", m, seconds-float64(m*60))
}

// openPlay finds a play of the project here, and the build it's replayed with.
func openPlay(ref string) (string, *plays.Play, plays.Build, error) {
	root, err := projectRoot()
	if err != nil {
		return "", nil, plays.Build{}, err
	}
	p, err := plays.Find(root, ref)
	if err != nil {
		return "", nil, plays.Build{}, err
	}
	return root, p, plays.ReadBuild(filepath.Join(root, "build")), nil
}

// openMoment is openPlay and a moment in the play.
func openMoment(ref, at string) (string, *plays.Play, plays.Build, uint64, error) {
	root, p, b, err := openPlay(ref)
	if err != nil {
		return "", nil, b, 0, err
	}
	f, err := p.FrameAt(at)
	return root, p, b, f, err
}

// playListing is a play as `jm plays` lists it.
type playListing struct {
	ID      string  `json:"id"`
	Started string  `json:"started"`
	Seconds float64 `json:"seconds"`
	Frames  uint64  `json:"frames"`
	Markers int     `json:"markers"`
	Ended   string  `json:"ended"`
	Stale   bool    `json:"stale,omitempty"` // made with a build that played differently
}

type playListings []playListing

func listPlays(root string) (playListings, error) {
	all, err := plays.List(root)
	if err != nil {
		return nil, err
	}
	b := plays.ReadBuild(filepath.Join(root, "build"))
	listing := playListings{}
	for _, p := range all {
		listing = append(listing, playListing{p.ID, p.Meta.Started, p.Meta.Seconds, p.Meta.Frames, len(p.Meta.Markers),
			p.Meta.Ended, p.DriftFrom(b) == plays.GameChanged})
	}
	return listing, nil
}

func emitPlays(w io.Writer) error {
	root, err := projectRoot()
	if err != nil {
		return err
	}
	listing, err := listPlays(root)
	return printResult(w, listing, err)
}

func (l playListings) text() string {
	if len(l) == 0 {
		return "No plays recorded yet: `jm run` records each time you play (F8 marks a moment).\n"
	}
	var out strings.Builder
	for _, p := range l {
		note := ""
		if p.Markers > 0 {
			note = fmt.Sprintf(", %d marker(s)", p.Markers)
		}
		if p.Ended != "quit" {
			note += ", " + p.Ended
		}
		if p.Stale {
			note += ", older build"
		}
		fmt.Fprintf(&out, "%s  %s%s\n", p.ID, clock(p.Seconds), note)
	}
	return out.String()
}

// playOverview is a play at a glance: `jm plays show`, and play_show's answer
// (the timeline widget reads it, and checks JM against its own version).
type playOverview struct {
	plays.Summary
	Dir   string `json:"dir"`
	Stale bool   `json:"stale"` // made with a build that plays differently
	JM    string `json:"jm"`
}

func overviewOf(p *plays.Play, b plays.Build) (playOverview, error) {
	s, err := p.Summarize()
	return playOverview{s, p.Dir, p.DriftFrom(b) == plays.GameChanged, version}, err
}

func (o playOverview) text() string {
	var out strings.Builder
	fmt.Fprintf(&out, "%s: %s of %s (%d frames), %s\n", o.ID, clock(o.Seconds), o.Game, o.Frames, endedText(o.Ended))
	if o.Stale {
		fmt.Fprintln(&out, "(made with an older build: replays may go differently; verify says)")
	}
	for _, span := range o.Scenes {
		fmt.Fprintf(&out, "  %s–%s  %s\n", clock(span.From), clock(span.To), span.Scene)
	}
	for _, v := range o.Values {
		fmt.Fprintf(&out, "  %s\n", valueLine(o.Summary, v))
	}
	for _, m := range o.Markers {
		note := ""
		if m.Note != "" {
			note = fmt.Sprintf(": %q", m.Note)
		}
		fmt.Fprintf(&out, "  marker %d at %s (frame %d, %s)%s\n", m.N, clock(m.Time), m.Frame, m.Scene, note)
	}
	fmt.Fprintf(&out, "  %d thumbnails in %s\n", len(o.Thumbs), filepath.Join(o.Dir, "thumbs"))
	return out.String()
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

// stateResult is the game's state at a moment of a play.
type stateResult struct {
	Play   string            `json:"play"`
	Frame  uint64            `json:"frame"`
	State  json.RawMessage   `json:"state"`
	Errors []json.RawMessage `json:"errors"` // what the game logged on the way
}

func stateAt(root string, p *plays.Play, f uint64, parts []string) (stateResult, error) {
	g, err := startGame(root, gameOptions{Play: p, Until: f + 1})
	if err != nil {
		return stateResult{}, err
	}
	defer g.close()
	if err := g.to(f); err != nil {
		return stateResult{}, err
	}
	if len(parts) == 0 {
		// Everything but the draw list (a frame's every quad): what the game
		// is, not how it's drawn; "draw" asks for it.
		parts = []string{"time", "scene", "entities", "session", "save", "ui", "replay"}
	}
	reply, err := g.do("state " + strings.Join(parts, " "))
	if err != nil {
		return stateResult{}, err
	}
	return stateResult{p.ID, f, reply["state"], nonNil(g.errors)}, nil
}

func nonNil(v []json.RawMessage) []json.RawMessage {
	if v == nil {
		return []json.RawMessage{}
	}
	return v
}

// frameSource is where an image of a moment came from: a replay (and how its
// build differs from the play's), or the nearest thumbnail when nothing here
// can replay it.
type frameSource struct {
	Kind  string      `json:"kind"`            // "replay" or "thumbnail"
	Drift plays.Drift `json:"drift,omitempty"` // a replay's
	Frame uint64      `json:"frame,omitempty"` // a thumbnail's
	Why   string      `json:"why,omitempty"`   // why it's a thumbnail
}

func (s frameSource) text() string {
	switch {
	case s.Kind == "thumbnail":
		return fmt.Sprintf("thumbnail of frame %d (%s)", s.Frame, s.Why)
	case s.Drift == plays.GameChanged:
		return "replay with the current build (the game changed since this play: not what the player saw)"
	case s.Drift == plays.LookChanged:
		return "replay, drawn with the current build (it plays the same, but its look changed since: images, shaders or UI differ from what the player saw)"
	}
	return "replay"
}

// frameResult is an image of a moment of a play.
type frameResult struct {
	Play   string      `json:"play"`
	Frame  uint64      `json:"frame"`
	Time   float64     `json:"time"`
	Path   string      `json:"path"`
	Source frameSource `json:"source"`
}

func (r frameResult) text() string { return r.Path + "\n" }

// frameImage is an image of frame f: replayed with GL when there's a display
// (or software GL), else the nearest thumbnail. A replay goes to out, or by
// default to the play's frames, kept per build and engine.
func frameImage(root string, p *plays.Play, b plays.Build, f uint64, out string) (frameResult, error) {
	r := frameResult{Play: p.ID, Frame: f, Time: p.TimeOf(f)}
	key := frameCacheKey(b.Look)
	cached := out == "" && key != ""
	if out == "" {
		out = p.FramePath(key, f)
	}
	out, _ = filepath.Abs(out)
	replay := frameSource{Kind: "replay", Drift: p.DriftFrom(b)}
	if _, err := os.Stat(out); err == nil && cached {
		r.Path, r.Source = out, replay
		return r, nil
	}
	why := "nothing here can draw: replaying needs a display or software GL (xvfb-run)"
	if hasDisplay() {
		err := replayImage(root, p, f, out)
		if err == nil {
			r.Path, r.Source = out, replay
			return r, nil
		}
		why = "the replay failed: " + err.Error()
	}
	thumb, ok := p.NearestThumb(f)
	if !ok {
		return r, fmt.Errorf("no image: %s, and the play has no thumbnails", why)
	}
	r.Path, r.Source = thumb.Path, frameSource{Kind: "thumbnail", Frame: thumb.Frame, Why: why}
	return r, nil
}

func replayImage(root string, p *plays.Play, f uint64, out string) error {
	g, err := startGame(root, gameOptions{Play: p, Until: f + 1, GL: true})
	if err != nil {
		return err
	}
	defer g.close()
	if err := g.to(f); err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		return err
	}
	_, err = g.do("capture " + out)
	return err
}

// frameCacheKey names what drew a replayed frame: the build's look and the
// engine binary; either changing draws new ones.
func frameCacheKey(look string) string {
	if look == "" {
		return ""
	}
	engine := "unknown"
	if path, err := resolveEnginePath(); err == nil {
		if info, err := os.Stat(path); err == nil {
			engine = fmt.Sprintf("%s\x00%d\x00%d", path, info.Size(), info.ModTime().UnixNano())
		}
	}
	sum := sha256.Sum256([]byte(look + "\x00" + engine))
	return hex.EncodeToString(sum[:])[:16]
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

func drivePlay(ref, at string) error {
	root, p, _, f, err := openMoment(ref, at)
	if err != nil {
		return err
	}
	g, err := startGame(root, gameOptions{Play: p, Until: f + 1, GL: hasDisplay()})
	if err != nil {
		return err
	}
	defer g.close()
	if err := g.to(f); err != nil {
		return err
	}
	// Hand over: the caller's commands go to the engine, its answers come back.
	fmt.Printf(`{"ok":true,"ready":true,"frame":%d,"play":%q}`+"\n", g.frame, p.ID)
	go func() {
		_, _ = io.Copy(g.in, os.Stdin)
		_ = g.in.Close()
	}()
	for g.out.Scan() {
		fmt.Println(g.out.Text())
	}
	return nil
}

// resumePlay hands the game to the person at frame f of a play: fast-forwarded
// there, then theirs, recorded as a new play.
func resumePlay(ref, at string) error {
	root, p, b, f, err := openMoment(ref, at)
	if err != nil {
		return err
	}
	engine, err := resolveEnginePath()
	if err != nil {
		return err
	}
	record, err := plays.Create(root, b, version)
	if err != nil {
		return err
	}
	// The new play replays the old one up to f without drawing it.
	p.CopyThumbs(record, f)
	fmt.Fprintf(os.Stderr, "Resuming %s at frame %d (fast-forwarding there); recording as %s\n", p.ID, f, filepath.Base(record))
	cmd := exec.Command(engine, ".")
	cmd.Dir = b.Dir
	cmd.Env = append(os.Environ(), "JM_PLAY_SESSION="+p.Dir, fmt.Sprintf("JM_PLAY_UNTIL=%d", f), "JM_PLAY_THEN=live",
		"JM_RECORD_DIR="+record)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	return cmd.Run()
}

// verifyResult is whether a play replays the same with the current build.
type verifyResult struct {
	Play      string            `json:"play"`
	Frames    uint64            `json:"frames,omitempty"`
	Same      *bool             `json:"same"` // null: it can't be checked (Reason says why)
	DiffersBy *uint64           `json:"differsBy,omitempty"`
	Drift     plays.Drift       `json:"drift,omitempty"`
	Reason    string            `json:"reason,omitempty"`
	Errors    []json.RawMessage `json:"errors"`
}

func verify(root string, p *plays.Play, b plays.Build) (verifyResult, error) {
	v := verifyResult{Play: p.ID, Errors: []json.RawMessage{}}
	if p.Meta.Gamepad {
		// Gamepads aren't recorded: the replay would go differently whatever the build.
		v.Reason = "played with a gamepad: not replayable"
		return v, nil
	}
	last, err := p.FrameAt("end")
	if err != nil {
		return v, err
	}
	g, err := startGame(root, gameOptions{Play: p})
	if err != nil {
		return v, err
	}
	defer g.close()
	if err := g.to(last); err != nil {
		return v, err
	}
	reply, err := g.do("state replay")
	if err != nil {
		return v, err
	}
	var state struct {
		Replay struct {
			Diverged *uint64 `json:"diverged"`
		} `json:"replay"`
	}
	_ = json.Unmarshal(reply["state"], &state)
	same := state.Replay.Diverged == nil
	v.Frames, v.Same, v.DiffersBy, v.Drift, v.Errors = last+1, &same, state.Replay.Diverged, p.DriftFrom(b), nonNil(g.errors)
	return v, nil
}

func (v verifyResult) text() string {
	var out strings.Builder
	switch {
	case v.Same == nil:
		fmt.Fprintf(&out, "%s can't be checked: it was %s\n", v.Play, v.Reason)
	case *v.Same:
		fmt.Fprintf(&out, "%s replays the same through its %d frames\n", v.Play, v.Frames)
	default:
		cause := "something in the game isn't deterministic: the build is the one it was played with"
		if v.Drift == plays.GameChanged {
			cause = "the game changed since it was played"
		}
		fmt.Fprintf(&out, "%s goes differently now: by frame %d (%s)\n", v.Play, *v.DiffersBy, cause)
	}
	if len(v.Errors) > 0 {
		fmt.Fprintf(&out, "%d error(s) on the way\n", len(v.Errors))
	}
	return out.String()
}

type pruned struct {
	Removed int `json:"removed"`
}

func (p pruned) text() string { return fmt.Sprintf("removed %d play(s)\n", p.Removed) }
