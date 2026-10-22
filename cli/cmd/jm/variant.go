package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/copies"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"

	"github.com/Jumballaya/Journeyman-Engine/internal/jmfolder"
	"github.com/spf13/cobra"
)

// Variants: other takes on the game, each a git branch checked out in its own
// folder under .jm/variants (a git worktree), so it builds and plays beside
// the game without touching it. Merge one that works; drop one that doesn't.

var variantFrom, variantPlay string
var variantFrame int
var variantSandbox bool

var variantCmd = &cobra.Command{
	Use:   "variant",
	Short: "Other takes on the game, side by side (git worktrees): try, compare, merge or drop",
	Long: `A variant is another take on the game: a branch of its history checked out
in .jm/variants/<name>, built and played on its own, so trying an idea never
touches the game. Compare shows both at the same moment; merge brings a
variant's changes into the game; drop throws it away.

  jm variant                      the variants (--json for tools)
  jm variant new <name> [--from v] a variant from the last commit (or version v)
  jm variant play <name>          play it in its own window
  jm variant compare <a> [b] [--frame 120] [--play id]
                                  what differs, and a frame of each (b: the game)
                                  at the same moment, with a recorded play's inputs
  jm variant merge <name>         commit what it has and merge it into the game
  jm variant drop <name>          remove its folder (its branch stays unless merged)

The variant's folder is a whole game: open_game it (MCP) or cd into it to
work there.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		return inGameRoot(func(root string) error {
			list := variantsOf(root)
			if jsonOutput {
				return writeJSON(cmd.OutOrStdout(), map[string]any{"variants": list})
			}
			if len(list) == 0 {
				fmt.Fprintln(cmd.OutOrStdout(), "No variants: jm variant new <name> makes one.")
			}
			for _, v := range list {
				fmt.Fprintf(cmd.OutOrStdout(), "%s  %s  (%s)\n", v.Name, v.Dir, v.summary())
			}
			return nil
		})
	},
}

func init() {
	variantCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	sub := func(use, short string, args cobra.PositionalArgs, run func(w io.Writer, root string, args []string) error) *cobra.Command {
		c := &cobra.Command{Use: use, Short: short, Args: args, RunE: func(cmd *cobra.Command, args []string) error {
			return inGameRoot(func(root string) error { return run(cmd.OutOrStdout(), root, args) })
		}}
		variantCmd.AddCommand(c)
		return c
	}
	n := sub("new <name>", "Make a variant from the last commit", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		v, err := newVariant(root, args[0], variantFrom)
		if err == nil {
			fmt.Fprintf(w, "Made variant %s at %s (from %s; uncommitted changes stay in the game).\n", v.Name, v.Dir, orLast(variantFrom))
		}
		return err
	})
	n.Flags().StringVar(&variantFrom, "from", "", "the version to start from (jm vcs log; default: the last commit)")
	sub("play <name>", "Play a variant in its own window", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		v, err := findVariant(root, args[0])
		if err == nil {
			err = playGame(nil, v.Dir)
		}
		if err == nil {
			fmt.Fprintf(w, "Variant %s is open to play.\n", v.Name)
		}
		return err
	})
	c := sub("compare <a> [b]", "What differs, and a frame of each at the same moment", cobra.RangeArgs(1, 2), func(w io.Writer, root string, args []string) error {
		b := "game"
		if len(args) == 2 {
			b = args[1]
		}
		cmp, err := compareVariants(nil, root, args[0], b, variantFrame, variantPlay, variantSandbox)
		if err != nil {
			return err
		}
		if jsonOutput {
			return writeJSON(w, cmp)
		}
		fmt.Fprint(w, cmp.text())
		return nil
	})
	c.Flags().IntVar(&variantFrame, "frame", 120, "the frame to show both at")
	c.Flags().BoolVar(&variantSandbox, "sandbox", false, "a is a sandbox (b: the game): its files that differ from the game's now")
	c.Flags().StringVar(&variantPlay, "play", "", "replay this recorded play's inputs in both (plays list; default: none)")
	sub("merge <name>", "Commit what a variant has and merge it into the game", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		m, err := mergeVariant(root, args[0])
		if err == nil {
			fmt.Fprintf(w, "Merged: %s (%d files).\n", m.Subject, len(m.Files))
		}
		return err
	})
	sub("drop <name>", "Remove a variant's folder", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		kept, err := dropVariant(root, args[0])
		if err == nil {
			fmt.Fprintln(w, dropText(args[0], kept))
		}
		return err
	})
}

func orLast(v string) string {
	if v == "" {
		return "the last commit"
	}
	return v
}

// variant is one variant: its folder (the game in it), branch, and how it
// stands against the game: commits each has that the other doesn't, and its
// uncommitted changes.
type variant struct {
	Name    string `json:"name"`
	Dir     string `json:"dir"`
	Branch  string `json:"branch"`
	Ahead   int    `json:"ahead"`
	Behind  int    `json:"behind"`
	Changed int    `json:"changed"`
	Subject string `json:"subject"` // its last commit's
	thumb   string
}

func (v variant) summary() string {
	parts := []string{fmt.Sprintf("%d commits ahead", v.Ahead)}
	if v.Behind > 0 {
		parts = append(parts, fmt.Sprintf("%d behind", v.Behind))
	}
	if v.Changed > 0 {
		parts = append(parts, fmt.Sprintf("%d files changed", v.Changed))
	}
	return strings.Join(parts, ", ") + "; " + v.Subject
}

const variantPrefix = "variant/"

var variantName = regexp.MustCompile(`^[a-z0-9][a-z0-9_-]{0,39}$`)

// variantsOf is the game's variants, by name.
func variantsOf(root string) []variant {
	out := []variant{}
	store, _ := copies.OpenStore(root, copies.Variant, false)
	entries, _ := os.ReadDir(store) // "": none
	for _, e := range entries {
		dir := vcs.GameIn(root, filepath.Join(store, e.Name()))
		branch := variantPrefix + e.Name()
		if !e.IsDir() || vcs.Branch(dir) != branch {
			continue // not one of ours (or half made)
		}
		v := variant{Name: e.Name(), Dir: dir, Branch: branch, Changed: len(vcs.Status(dir)), thumb: thumbOf(root, copies.Variant, e.Name(), dir)}
		v.Ahead, v.Behind = vcs.Distance(root, branch)
		if log, _ := vcs.Log(dir, 1); len(log) > 0 {
			v.Subject = log[0].Subject
		}
		out = append(out, v)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

func findVariant(root, name string) (variant, error) {
	for _, v := range variantsOf(root) {
		if v.Name == name {
			return v, nil
		}
	}
	return variant{}, fmt.Errorf("no variant %q (jm variant lists them)", name)
}

func newVariant(root, name, from string) (variant, error) {
	if !variantName.MatchString(name) || name == "game" { // "game" is the game itself in compare
		return variant{}, fmt.Errorf("%q can't be a variant's name: lowercase letters, digits, - and _, e.g. floaty-jump", name)
	}
	if log, err := vcs.Log(root, 1); err != nil || len(log) == 0 {
		return variant{}, fmt.Errorf("a variant starts from a commit, and the game has none yet: jm vcs commit first")
	}
	store, err := copies.OpenStore(root, copies.Variant, true)
	if err != nil {
		return variant{}, err
	}
	dir := filepath.Join(store, name)
	if exists(dir) {
		return variant{}, fmt.Errorf("variant %s already exists", name)
	}
	// Variants live in .jm: an older game that doesn't ignore it would commit them.
	if rel, _ := filepath.Rel(root, store); !vcs.Ignored(root, filepath.ToSlash(rel)) {
		if _, err := ensureGitignoreLines(root, []string{".jm/"}); err != nil {
			return variant{}, err
		}
	}
	game, err := vcs.AddWorktree(root, dir, variantPrefix+name, from)
	if err != nil {
		return variant{}, err
	}
	v, err := variant{}, ignoreItsJM(game)
	if err == nil {
		v, err = findVariant(root, name)
	}
	if err != nil {
		vcs.UndoWorktree(root, dir, variantPrefix+name) // only what this made: -b made the branch
	}
	return v, err
}

// ignoreItsJM makes a variant's .jm ignore itself (a version from before the
// game ignored .jm would commit its plays), never writing through a link there.
func ignoreItsJM(game string) error {
	dir := filepath.Join(game, ".jm")
	if info, err := os.Lstat(dir); err == nil && !info.IsDir() {
		return fmt.Errorf("that version's .jm isn't a folder (a link or a file): a variant can't start from it")
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	ignore := filepath.Join(dir, ".gitignore")
	if info, err := os.Lstat(ignore); err == nil && !info.Mode().IsRegular() {
		return fmt.Errorf("that version's .jm/.gitignore isn't a file (a link or a folder): a variant can't start from it")
	}
	return os.WriteFile(ignore, []byte("*\n"), 0o644)
}

// mergeVariant commits what the variant has, then merges it into the game.
func mergeVariant(root, name string) (vcs.Version, error) {
	v, err := findVariant(root, name)
	if err != nil {
		return vcs.Version{}, err
	}
	if _, err := vcs.Commit(v.Dir, ""); err != nil && err != vcs.ErrNothing {
		return vcs.Version{}, err
	}
	return vcs.Merge(root, v.Branch, "Merge variant "+name)
}

func dropVariant(root, name string) (bool, error) {
	v, err := findVariant(root, name)
	if err != nil {
		return false, err
	}
	_ = os.Remove(compareImage(root, string(copies.Variant), name)) // a new one of that name isn't this
	return vcs.RemoveWorktree(root, filepath.Join(copies.Store(root, copies.Variant), name), v.Branch)
}

func dropText(name string, kept bool) string {
	if kept {
		return fmt.Sprintf("Dropped variant %s; its commits stay on branch %s%s (git branch -D deletes it).", name, variantPrefix, name)
	}
	return "Dropped variant " + name + "."
}

// playGame opens a game in its own window for the person (built first if it
// needs it), with more of jm run's flags (--fullscreen) if given.
func playGame(c *toolCall, dir string, flags ...string) error {
	if err := freshBuild(c, dir); err != nil {
		return err
	}
	return startDetached(jmCommand(context.Background(), dir, append([]string{"run"}, flags...)...), "the game")
}

// comparison is two takes on the game: the files that differ (a scene's per
// entity) and a frame of each at the same moment ("" when it couldn't run).
type comparison struct {
	A, B    string       `json:"-"`
	Changes []fileChange `json:"changes"`
	Frame   int          `json:"frame"`
	Frames  [2]string    `json:"frames"` // image files
	Errors  [2]string    `json:"errors"`
	Sandbox bool         `json:"sandbox,omitempty"` // a is a sandbox: Changes are its files that differ from the game's now
}

func (c comparison) text() string {
	var b strings.Builder
	if c.Sandbox {
		fmt.Fprintf(&b, "Sandbox %s differs from the game in %d files.\n", c.A, len(c.Changes))
	} else {
		fmt.Fprintf(&b, "%s changed %d files since it split from %s.\n", c.A, len(c.Changes), c.B)
	}
	b.WriteString(changesText(c.Changes))
	for i, name := range []string{c.A, c.B} {
		if c.Errors[i] != "" {
			fmt.Fprintf(&b, "%s couldn't run: %s\n", name, c.Errors[i])
		} else {
			fmt.Fprintf(&b, "%s at frame %d: %s\n", name, c.Frame, c.Frames[i])
		}
	}
	return b.String()
}

// compareVariants compares variant a with b ("game": the game itself): what
// a's commits changed since they split, uncommitted work aside, and a frame
// of each. A sandbox a is compared with the game's files as they are now: it
// is a copy of them, not a branch.
func compareVariants(c *toolCall, root, a, b string, frame int, play string, sandbox bool) (comparison, error) {
	cmp := comparison{A: a, B: b, Frame: max(frame, 1), Sandbox: sandbox}
	if sandbox && b != "game" {
		return cmp, fmt.Errorf("a sandbox is compared with the game, not %s", b)
	}
	sides := [2]struct{ dir, rev, kind string }{}
	for i, name := range []string{a, b} {
		if sandbox && i == 0 { // a sandbox may be named game
			sb, err := findSandbox(root, name)
			if err != nil {
				return cmp, err
			}
			sides[i].dir, sides[i].kind = sb.Dir, string(copies.Sandbox)
			continue
		}
		if name == "game" {
			sides[i].dir, sides[i].rev, sides[i].kind = root, "HEAD", "game"
			continue
		}
		v, err := findVariant(root, name)
		if err != nil {
			return cmp, err
		}
		sides[i].dir, sides[i].rev, sides[i].kind = v.Dir, "refs/heads/"+v.Branch, string(copies.Variant)
	}
	marks, before, after := map[string]string{}, func(string) []byte { return nil }, func(string) []byte { return nil }
	if cmp.Sandbox {
		marks = differences(root, sides[0].dir)
		before = func(path string) []byte {
			data, _ := os.ReadFile(filepath.Join(root, filepath.FromSlash(path)))
			return data
		}
		after = func(path string) []byte {
			data, _ := os.ReadFile(filepath.Join(sides[0].dir, filepath.FromSlash(path)))
			return data
		}
	} else {
		var base string
		var err error
		if marks, base, err = vcs.Changed(root, sides[1].rev, sides[0].rev); err != nil {
			return cmp, err
		}
		before = func(path string) []byte { return vcs.Show(root, base, path) }
		after = func(path string) []byte { return vcs.Show(root, sides[0].rev, path) }
	}
	cmp.Changes = []fileChange{}
	for path, mark := range marks {
		c := fileChange{Path: path, Status: mark}
		if strings.HasSuffix(path, ".scene.json") {
			c.Entities = sceneChanges(before(path), after(path))
		}
		cmp.Changes = append(cmp.Changes, c)
	}
	sort.Slice(cmp.Changes, func(i, j int) bool { return cmp.Changes[i].Path < cmp.Changes[j].Path })
	var err error
	var p *plays.Play
	if play != "" {
		if p, err = plays.Find(root, play); err != nil {
			return cmp, err
		}
	}
	if _, err := jmfolder.Make(root, "compare"); err != nil {
		return cmp, err
	}
	for i, s := range sides {
		cmp.Frames[i], err = frameOf(c, i, s.dir, uint64(cmp.Frame), p, compareImage(root, s.kind, []string{a, b}[i]))
		if err != nil {
			cmp.Errors[i] = err.Error()
		}
	}
	return cmp, nil
}

// compareImage is where a comparison keeps a side's frame (kind: variant,
// sandbox or game), kept for a look and as its picture till it's played; the
// next compare of it replaces it.
func compareImage(root, kind, name string) string {
	file := kind + "-" + name + ".png"
	if kind == "game" {
		file = "game.png"
	}
	return filepath.Join(root, ".jm", "compare", file)
}

// thumbOf is a variant's or sandbox's picture: its newest play's, else its
// last compare's, else a golden frame (maybe the game's, copied with it),
// else the game's own (a fresh one is the game).
func thumbOf(root string, kind copies.Kind, name, dir string) string {
	if thumb := projects.PlayThumb(dir); thumb != "" {
		return thumb
	}
	if path := compareImage(root, string(kind), name); exists(path) {
		return path
	}
	if thumb := projects.GoldenThumb(dir); thumb != "" {
		return thumb
	}
	return projects.Thumb(root)
}

// frameOf runs a game headless (built first if it needs it) to a frame, with
// a play's inputs when given, and saves that frame as out: side 0 or 1 of a comparison, for c's progress.
func frameOf(c *toolCall, side int, dir string, frame uint64, p *plays.Play, out string) (string, error) {
	if _, err := resolveEnginePath(); err != nil { // before a build that would be for nothing
		return "", err
	}
	if err := freshBuild(c, dir); err != nil {
		return "", errors.New(clip(err.Error(), 400))
	}
	defer c.followEngineLog(dir)()
	o := gameOptions{GL: true, Seed: new(uint64), Stop: c.context()}
	if p != nil {
		o.Play, o.Until, o.Seed = p, frame+1, nil
	}
	g, err := startGame(dir, o)
	if err != nil {
		return "", err
	}
	defer g.close()
	if err := g.runTo(c, frame, float64(side)*float64(frame+1), 2*float64(frame+1)); err != nil {
		return "", err
	}
	path, err := g.capture()
	if err != nil {
		return "", err
	}
	data, err := os.ReadFile(path) // copied: the driver's folder may be on another drive
	if err != nil {
		return "", err
	}
	return out, os.WriteFile(out, data, 0o644)
}
