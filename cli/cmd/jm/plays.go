package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"maps"
	"os"
	"path/filepath"
	"runtime"
	"slices"
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
or "latest-1", "latest-2" for the ones before: yours, as an agent's own
runs (drive_start with record) go by their ids. A moment in it is a frame ("420"), a time
("12.5s", "1:05"), a marker ("marker:2" or "m2"), "start" or "end".

  jm plays                         the plays, newest first
  jm plays show [play]             what happened: scenes, values over time, markers
  jm plays state [play] [moment]   the game's state then: all but the draw list,
                                   or the driver's parts (session, ui, draw, tag=Name, ...)
  jm plays frame [play] [moment]   an image of that moment
  jm plays inputs [play] [from] [to]  what was pressed, and for how long
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
	// A command on the play of the project here.
	rooted := func(run func(root string, cmd *cobra.Command, args []string) error) func(*cobra.Command, []string) error {
		return func(cmd *cobra.Command, args []string) error {
			root, err := projectRoot()
			if err != nil {
				return err
			}
			return run(root, cmd, args)
		}
	}
	sub("list", "The recorded plays, newest first", 0, func(cmd *cobra.Command, _ []string) error {
		return emitPlays(cmd.OutOrStdout())
	})
	sub("show [play]", "What happened in a play", 1, rooted(func(root string, cmd *cobra.Command, args []string) error {
		p, b, err := openPlay(root, arg(args, 0))
		if err != nil {
			return err
		}
		o, err := overviewOf(root, p, b)
		return printResult(cmd.OutOrStdout(), o, err)
	}))
	sub("state [play] [moment] [part...]", "The game's state at a moment (replayed)", 64, rooted(func(root string, cmd *cobra.Command, args []string) error {
		p, _, f, err := openMoment(root, arg(args, 0), arg(args, 1))
		if err != nil {
			return err
		}
		s, err := stateAt(nil, root, p, f, args[min(len(args), 2):])
		if err != nil {
			return err
		}
		return writeJSON(cmd.OutOrStdout(), s)
	}))
	frame := sub("frame [play] [moment]", "An image of a moment (replayed with GL, else the nearest thumbnail)", 2,
		rooted(func(root string, cmd *cobra.Command, args []string) error {
			p, b, f, err := openMoment(root, arg(args, 0), arg(args, 1))
			if err != nil {
				return err
			}
			out, _ := cmd.Flags().GetString("out")
			r, err := frameImage(nil, root, p, b, f, out)
			if err == nil && r.Source.Kind != "replay" {
				fmt.Fprintf(cmd.ErrOrStderr(), "not frame %d: the %s\n", f, r.Source.text())
			}
			return printResult(cmd.OutOrStdout(), r, err)
		}))
	frame.Flags().String("out", "", "where to write the PNG (default: the play's folder)")
	sub("inputs [play] [from] [to]", "What the person pressed, and how long (default: the whole play)", 3, rooted(func(root string, cmd *cobra.Command, args []string) error {
		l, err := pressesBetween(root, arg(args, 0), arg(args, 1), arg(args, 2))
		return printResult(cmd.OutOrStdout(), l, err)
	}))
	sub("drive [play] [moment]", "The stepped driver, starting at a moment of the play", 2, rooted(func(root string, cmd *cobra.Command, args []string) error {
		return drivePlay(root, arg(args, 0), arg(args, 1))
	}))
	resume := sub("resume [play] [moment]", "Play on yourself from a moment (recorded as a new play)", 2, rooted(func(root string, cmd *cobra.Command, args []string) error {
		in, _ := cmd.Flags().GetString("in")
		if in == "" {
			in = root
		}
		return resumePlay(root, in, arg(args, 0), arg(args, 1))
	}))
	resume.Flags().String("in", "", "the game to play it in: a variant's or sandbox's folder (default: this game; built as it is)")
	sub("verify [play]", "Replay a play to its end: does it go the same way?", 1, rooted(func(root string, cmd *cobra.Command, args []string) error {
		p, b, err := openPlay(root, arg(args, 0))
		if err != nil {
			return err
		}
		v, err := verify(nil, root, p, b)
		return printResult(cmd.OutOrStdout(), v, err)
	}))
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

// newPlay is a new play of the game at root, made with build b and played by
// by (plays.Agent, or "" for the person; older unmarked plays go first, but
// those the run reads), and what a run needs to record into it.
func newPlay(root string, b plays.Build, by string, reads ...*plays.Play) (string, []string, error) {
	spare := make([]string, len(reads))
	for i, p := range reads {
		spare[i] = p.Dir
	}
	_, _ = plays.Prune(root, keptPlays, true, spare...)
	dir, err := plays.Create(root, b, version, by)
	if err != nil {
		return "", nil, err
	}
	return dir, []string{"JM_RECORD_DIR=" + dir}, nil
}

// personsPlay is newPlay for a run a person plays: play_live steers it too.
func personsPlay(root string, b plays.Build, reads ...*plays.Play) (string, []string, error) {
	dir, env, err := newPlay(root, b, "", reads...)
	if err != nil {
		return "", nil, err
	}
	return dir, append(env, "JM_CONTROL="+newLiveDir(root)), nil
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

// openPlay finds a play of the game at root, and the build it's replayed with.
func openPlay(root, ref string) (*plays.Play, plays.Build, error) {
	p, err := plays.Find(root, ref)
	if err != nil {
		return nil, plays.Build{}, err
	}
	return p, plays.ReadBuild(filepath.Join(root, "build")), nil
}

// openMoment is openPlay and a moment in the play.
func openMoment(root, ref, at string) (*plays.Play, plays.Build, uint64, error) {
	p, b, err := openPlay(root, ref)
	if err != nil {
		return nil, b, 0, err
	}
	f, err := p.FrameAt(at)
	return p, b, f, err
}

// playListing is a play as `jm plays` lists it.
type playListing struct {
	ID      string   `json:"id"`
	Started string   `json:"started"`
	Seconds float64  `json:"seconds"`
	Frames  uint64   `json:"frames"`
	Markers int      `json:"markers"`
	Ended   string   `json:"ended"`
	Stale   bool     `json:"stale,omitempty"` // made with a build that played differently
	Marks   []string `json:"marks,omitempty"` // "m1 0:03.0", plus a driver marker's note
	// The markers that are moments (picks: what the person pointed at), by number.
	Moments []int  `json:"moments,omitempty"`
	By      string `json:"by,omitempty"` // "agent": an agent's own run, not the person's
}

// playListings are the plays, and the play folders that can't be read.
type playListings struct {
	Plays      []playListing `json:"plays"`
	Unreadable []string      `json:"unreadable,omitempty"` // jm plays show <id> says why; prune removes them
}

func listPlays(root string) (playListings, error) {
	all, err := plays.List(root)
	if err != nil {
		return playListings{}, err
	}
	b := plays.ReadBuild(filepath.Join(root, "build"))
	listing := playListings{Plays: []playListing{}, Unreadable: plays.Unreadable(root)}
	for _, p := range all {
		listing.Plays = append(listing.Plays, playListing{p.ID, p.Meta.Started, p.Meta.Seconds, p.Meta.Frames, len(p.Meta.Markers),
			p.Meta.Ended, p.DriftFrom(b) == plays.GameChanged, marks(p.Meta.Markers), moments(p.Meta.Markers), p.Info().By})
	}
	return listing, nil
}

// marks are a play's markers as "m1 0:03.0" lines (": note" for a driver's,
// "picked Pip, Cloud" for a moment), for listings.
func marks(markers []plays.Marker) []string {
	var out []string
	for _, m := range markers {
		line := fmt.Sprintf("m%d %s", m.N, clock(m.Time))
		if m.Note != "" {
			line += ": " + m.Note
		}
		if len(m.Picks) > 0 {
			line += " · picked " + strings.Join(pickNames(m), ", ")
		}
		out = append(out, line)
	}
	return out
}

func moments(markers []plays.Marker) []int {
	var out []int
	for _, m := range markers {
		if len(m.Picks) > 0 {
			out = append(out, m.N)
		}
	}
	return out
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
	var out strings.Builder
	if len(l.Plays) == 0 {
		out.WriteString("No plays recorded yet: `jm run` records each time you play (F8 marks a moment).\n")
	}
	for _, p := range l.Plays {
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
		if p.By == plays.Agent {
			note += ", an agent's run"
		}
		fmt.Fprintf(&out, "%s  %s%s\n", p.ID, clock(p.Seconds), note)
	}
	if len(l.Unreadable) > 0 {
		fmt.Fprintf(&out, "%d play folder(s) can't be read: %s (jm plays show <id> says why; jm plays prune removes them)\n",
			len(l.Unreadable), strings.Join(l.Unreadable, ", "))
	}
	return out.String()
}

// playOverview is a play at a glance: `jm plays show`, and play_show's answer
// (the timeline widget reads it, and checks JM against its own version).
type playOverview struct {
	plays.Summary
	Game  string  `json:"game"` // its folder: what follow-up calls name
	Dir   string  `json:"dir"`
	Stale bool    `json:"stale"` // made with a build that plays differently
	JM    string  `json:"jm"`
	By    string  `json:"by,omitempty"`    // "agent": an agent's own run, not the person's
	At    *uint64 `json:"at,omitempty"`    // the frame play_show opens at
	Build string  `json:"build,omitempty"` // what replays are made with now (frameCacheKey): one of another is of an older game
}

func overviewOf(root string, p *plays.Play, b plays.Build) (playOverview, error) {
	s, err := p.Summarize()
	return playOverview{Summary: s, Game: evalOrSelf(root), Dir: p.Dir, Stale: p.DriftFrom(b) == plays.GameChanged, JM: version, By: p.Info().By, Build: frameCacheKey(b.Look)}, err
}

func (o playOverview) text() string {
	var out strings.Builder
	fmt.Fprintf(&out, "%s: %s of %s (%d frames), %s\n", o.ID, clock(o.Seconds), o.Name, o.Frames, endedText(o.Ended))
	if o.By == plays.Agent {
		fmt.Fprintln(&out, "(an agent's run, not the person's play)")
	}
	if o.Stale {
		fmt.Fprintln(&out, "(made with an older build: replays may go differently; verify says)")
	}
	for _, span := range o.Scenes {
		fmt.Fprintf(&out, "  %s–%s  %s\n", clock(span.From), clock(span.To), span.Scene)
	}
	// Values set once and never changed say little: one line, after those that moved.
	var unchanged []string
	for _, v := range o.Values {
		if v.Min == v.Max {
			unchanged = append(unchanged, fmt.Sprintf("%s %g (%s)", v.Key, v.First, clock(o.SampleT[v.Since])))
			continue
		}
		fmt.Fprintf(&out, "  %s\n", valueLine(o.Summary, v))
	}
	if len(unchanged) > 0 {
		fmt.Fprintf(&out, "  set, then never changed: %s\n", strings.Join(unchanged, ", "))
	}
	for _, m := range o.Markers {
		note := ""
		if m.Note != "" {
			note = fmt.Sprintf(": %q", m.Note)
		}
		fmt.Fprintf(&out, "  marker %d at %s (frame %d, %s)%s\n", m.N, clock(m.Time), m.Frame, m.Scene, note)
		if len(m.Pressed) > 0 {
			fmt.Fprintf(&out, "    pressed just before: %s\n", pressesText(m.Pressed))
		}
		out.WriteString(momentText(m))
	}
	fmt.Fprintf(&out, "  %d thumbnails in %s\n", len(o.Thumbs), filepath.Join(o.Dir, "thumbs"))
	return out.String()
}

// pressesText is presses on one line: "Space 0:12.3–0:12.7 (0.42 s), ArrowRight from 0:11.8 (held at the end)".
func pressesText(presses []plays.Press) string {
	parts := make([]string, len(presses))
	for i, p := range presses {
		parts[i] = fmt.Sprintf("%s %s–%s (%.2f s)", p.Input, clock(p.From), clock(p.To), p.To-p.From)
		if p.Open {
			parts[i] = fmt.Sprintf("%s from %s (held at the end)", p.Input, clock(p.From))
		}
	}
	return strings.Join(parts, ", ")
}

// pressList is what the person pressed between two moments of a play.
type pressList struct {
	Play    string        `json:"play"`
	From    float64       `json:"from"` // seconds
	To      float64       `json:"to"`
	Presses []plays.Press `json:"presses"`
}

func (l pressList) text() string {
	if len(l.Presses) == 0 {
		return fmt.Sprintf("%s %s–%s: nothing pressed\n", l.Play, clock(l.From), clock(l.To))
	}
	var out strings.Builder
	fmt.Fprintf(&out, "%s %s–%s:\n", l.Play, clock(l.From), clock(l.To))
	for _, p := range l.Presses {
		fmt.Fprintf(&out, "  %s\n", pressesText([]plays.Press{p}))
	}
	return out.String()
}

// pressesBetween is a play's presses from one moment to another (default: all of it).
func pressesBetween(root, ref, from, to string) (pressList, error) {
	p, _, err := openPlay(root, ref)
	if err != nil {
		return pressList{}, err
	}
	if from == "" {
		from = "start"
	}
	f, err := p.FrameAt(from)
	if err != nil {
		return pressList{}, err
	}
	t, err := p.FrameAt(to)
	if err != nil {
		return pressList{}, err
	}
	if t < f {
		f, t = t, f
	}
	presses, err := p.Presses(f, t)
	if os.IsNotExist(err) {
		err = fmt.Errorf("play %s has no inputs.jsonl: nothing was recorded", p.ID)
	}
	return pressList{p.ID, p.TimeOf(f), p.TimeOf(t), presses}, err
}

// valueLine is a session value that changed over the play: "score: 0 → 100", with
// where it started (when the game set it after the play began) and its range when
// that says more than its ends.
func valueLine(s plays.Summary, v plays.Series) string {
	line := fmt.Sprintf("%s: %g → %g", v.Key, v.First, v.Last)
	if v.Since > 0 && v.Since < len(s.SampleT) {
		line = fmt.Sprintf("%s: %g (set at %s) → %g", v.Key, v.First, clock(s.SampleT[v.Since]), v.Last)
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

func stateAt(c *toolCall, root string, p *plays.Play, f uint64, parts []string) (stateResult, error) {
	g, err := startGame(root, gameOptions{Play: p, Until: f + 1, Stop: c.context()})
	if err != nil {
		return stateResult{}, err
	}
	defer g.close()
	if err := g.runTo(c, f, 0, float64(f+1)); err != nil {
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
	Frame uint64      `json:"frame"`           // a thumbnail's (a replay's is the one asked for)
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
	Build  string      `json:"build,omitempty"` // what replayed it (frameCacheKey), as play_show says the current one
}

func (r frameResult) text() string { return r.Path + "\n" }

// frameImage is an image of frame f: replayed with GL when there's a display
// (or software GL), else the nearest thumbnail. A replay goes to out, or by
// default to the play's frames, kept per build and engine.
func frameImage(c *toolCall, root string, p *plays.Play, b plays.Build, f uint64, out string) (frameResult, error) {
	key := frameCacheKey(b.Look)
	r := frameResult{Play: p.ID, Frame: f, Time: p.TimeOf(f), Build: key}
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
	if canDraw() {
		err := replayImage(c, root, p, f, out)
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

func replayImage(c *toolCall, root string, p *plays.Play, f uint64, out string) error {
	g, err := startGame(root, gameOptions{Play: p, Until: f + 1, GL: true, Stop: c.context()})
	if err != nil {
		return err
	}
	defer g.close()
	if err := g.runTo(c, f, 0, float64(f+1)); err != nil {
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

// canDraw says whether GL can draw here (so captures work): macOS and
// Windows can, Linux with an X or Wayland display; nothing when JM_RENDERER=none
// says not to try, or a sandbox blocks windows (windowsBlocked).
func canDraw() bool {
	if os.Getenv("JM_RENDERER") == "none" || windowsBlocked() != nil {
		return false
	}
	return runtime.GOOS != "linux" || os.Getenv("DISPLAY") != "" || os.Getenv("WAYLAND_DISPLAY") != ""
}

func drivePlay(root, ref, at string) error {
	p, _, f, err := openMoment(root, ref, at)
	if err != nil {
		return err
	}
	g, err := startGame(root, gameOptions{Play: p, Until: f + 1, GL: canDraw()})
	if err != nil {
		return err
	}
	if err := g.to(f); err != nil {
		g.close()
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
	g.close()
	if state := g.cmd.ProcessState; state != nil && !state.Success() {
		return fmt.Errorf("the game stopped: %s (its log: %s)", state, engineLog)
	}
	return nil
}

// resumePlay hands the game in (root itself, or a variant or sandbox of it) to
// the person at a moment of root's play: fast-forwarded there, then theirs,
// recorded as a new play of in's.
func resumePlay(root, in, ref, at string) error {
	in, err := filepath.Abs(in) // the engine runs in it: paths from here would be read from there
	if err != nil {
		return err
	}
	p, err := plays.Find(root, ref)
	if err != nil {
		return err
	}
	f, err := p.FrameAt(at)
	if err != nil {
		return err
	}
	b := plays.ReadBuild(filepath.Join(in, "build"))
	engine, err := resolveEnginePath()
	if err != nil {
		return err
	}
	record, recording, err := personsPlay(in, b, p) // p: what it fast-forwards through
	if err != nil {
		return err
	}
	// The new play replays the old one through f without drawing it: the
	// person takes over where state, frame and drive show frame f.
	p.CopyThumbs(record, f+1)
	fmt.Fprintf(os.Stderr, "Resuming %s at frame %d (fast-forwarding there); recording as %s\n", p.ID, f, filepath.Base(record))
	cmd := engineRun(context.Background(), engine, in, b.Dir)
	cmd.Env = append(append(os.Environ(), "JM_PLAY_SESSION="+p.Dir, fmt.Sprintf("JM_PLAY_UNTIL=%d", f+1), "JM_PLAY_THEN=live"), recording...)
	if m, ok := p.Marker(at); ok { // a changed game's replay may not get there: it lands in the marker's scene
		cmd.Env = append(cmd.Env, fmt.Sprintf("JM_PLAY_MARKER=%d", m.N))
	}
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	return cmd.Run()
}

// verifyResult is whether a play replays the same with the current build.
type verifyResult struct {
	Play      string            `json:"play"`
	Frames    uint64            `json:"frames,omitempty"`
	Same      *bool             `json:"same"` // null: it can't be checked (Reason says why)
	DiffersBy *uint64           `json:"differsBy,omitempty"`
	DiffersAt float64           `json:"differsAt,omitempty"` // seconds into the play
	Drift     plays.Drift       `json:"drift,omitempty"`
	CheckedTo uint64            `json:"checkedTo"` // the last frame its timeline let the replay be checked at
	Changed   []valueChange     `json:"changed"`   // session values there that differ from the play's (null: not compared)
	Reason    string            `json:"reason,omitempty"`
	Errors    []json.RawMessage `json:"errors"`
}

// valueChange is a session value the play had (Was) and the replay has (Now); omitted when unset.
type valueChange struct {
	Key string          `json:"key"`
	Was json.RawMessage `json:"was,omitempty"`
	Now json.RawMessage `json:"now,omitempty"`
}

func verify(c *toolCall, root string, p *plays.Play, b plays.Build) (verifyResult, error) {
	v := verifyResult{Play: p.ID, Errors: []json.RawMessage{}}
	if p.Meta.Gamepad {
		// Gamepads aren't recorded: the replay would go differently whatever the build.
		v.Reason = "played with a gamepad: not replayable"
		return v, nil
	}
	if p.Recorded() < p.Meta.Frames {
		v.Reason = fmt.Sprintf("its recording stops at frame %d of %d (the game crashed while recording?): only that much replays", p.Recorded(), p.Meta.Frames)
		return v, nil
	}
	last, err := p.FrameAt("end")
	if err != nil {
		return v, err
	}
	g, err := startGame(root, gameOptions{Play: p, Stop: c.context()})
	if err != nil {
		return v, err
	}
	defer g.close()
	// How the play ended against how the replay does, at the timeline's last point.
	if samples, _ := p.Samples(); len(samples) > 0 {
		end := samples[len(samples)-1]
		v.CheckedTo = min(end.Frame, last)
		if end.Frame <= last {
			if err := g.runTo(c, end.Frame, 0, float64(last+1)); err != nil {
				return v, err
			}
			reply, err := g.do("state session")
			if err != nil {
				return v, err
			}
			var state struct {
				Session map[string]json.RawMessage `json:"session"`
			}
			_ = json.Unmarshal(reply["state"], &state)
			v.Changed = changedValues(end.Session, state.Session)
		}
	}
	if err := g.runTo(c, last, 0, float64(last+1)); err != nil {
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
	if !same {
		v.DiffersAt = p.TimeOf(*v.DiffersBy)
	}
	return v, nil
}

func orUnset(v json.RawMessage) string {
	if v == nil {
		return "unset"
	}
	return string(v)
}

// changedValues is what differs between two sets of session values, by key.
func changedValues(was, now map[string]json.RawMessage) []valueChange {
	keys := map[string]bool{}
	for k := range was {
		keys[k] = true
	}
	for k := range now {
		keys[k] = true
	}
	changed := []valueChange{}
	for _, k := range slices.Sorted(maps.Keys(keys)) {
		if a, b := canonicalJSON(was[k]), canonicalJSON(now[k]); !bytes.Equal(a, b) {
			changed = append(changed, valueChange{k, a, b})
		}
	}
	return changed
}

// canonicalJSON is a JSON value in one spelling (numbers kept exact), nil when unset.
func canonicalJSON(raw json.RawMessage) json.RawMessage {
	d := json.NewDecoder(bytes.NewReader(raw))
	d.UseNumber()
	var v any
	if len(raw) == 0 || d.Decode(&v) != nil {
		return nil
	}
	out, _ := json.Marshal(v)
	return out
}

func (v verifyResult) text() string {
	var out strings.Builder
	switch {
	case v.Same == nil:
		fmt.Fprintf(&out, "%s can't be checked: %s\n", v.Play, v.Reason)
	case *v.Same && v.CheckedTo+1 < v.Frames:
		fmt.Fprintf(&out, "%s replays the same as far as its timeline lets it be checked: frame %d of %d\n", v.Play, v.CheckedTo, v.Frames)
	case *v.Same:
		fmt.Fprintf(&out, "%s replays the same through its %d frames\n", v.Play, v.Frames)
	default:
		cause := "something in the game isn't deterministic: the build is the one it was played with"
		if v.Drift == plays.GameChanged {
			cause = "the game changed since it was played"
		}
		fmt.Fprintf(&out, "%s goes differently now, by %s (frame %d): %s\n", v.Play, clock(v.DiffersAt), *v.DiffersBy, cause)
		switch {
		case v.Changed == nil:
		case len(v.Changed) == 0:
			fmt.Fprintf(&out, "at frame %d (its timeline's end) the session values match the play's\n", v.CheckedTo)
		default:
			parts := make([]string, len(v.Changed))
			for i, c := range v.Changed {
				parts[i] = fmt.Sprintf("%s %s → %s", c.Key, orUnset(c.Was), orUnset(c.Now))
			}
			fmt.Fprintf(&out, "at frame %d (its timeline's end) it differs: %s\n", v.CheckedTo, strings.Join(parts, ", "))
		}
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
