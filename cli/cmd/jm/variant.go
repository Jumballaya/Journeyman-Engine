package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"

	"github.com/spf13/cobra"
)

// Variants: other takes on the game, each a git branch checked out in its own
// folder under .jm/variants (a git worktree), so it builds and plays beside
// the game without touching it. Merge one that works; drop one that doesn't.

var variantFrom, variantPlay string
var variantFrame int

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
			err = playGame(v.Dir)
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
		cmp, err := compareVariants(root, args[0], b, variantFrame, variantPlay)
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

func variantsDir(root string) string { return filepath.Join(root, ".jm", "variants") }

// variantsOf is the game's variants, by name.
func variantsOf(root string) []variant {
	out := []variant{}
	entries, _ := os.ReadDir(variantsDir(root))
	for _, e := range entries {
		dir := vcs.GameIn(root, filepath.Join(variantsDir(root), e.Name()))
		branch := variantPrefix + e.Name()
		if !e.IsDir() || vcs.Branch(dir) != branch {
			continue // not one of ours (or half made)
		}
		v := variant{Name: e.Name(), Dir: dir, Branch: branch, Changed: len(vcs.Status(dir)), thumb: projects.ThumbOf(dir)}
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
	dir := filepath.Join(variantsDir(root), name)
	if exists(dir) {
		return variant{}, fmt.Errorf("variant %s already exists", name)
	}
	if err := os.MkdirAll(variantsDir(root), 0o755); err != nil {
		return variant{}, err
	}
	// Variants live in .jm: an older game that doesn't ignore it would commit them.
	if !vcs.Ignored(root, ".jm/variants") {
		if _, err := ensureGitignoreLines(root, []string{".jm/"}); err != nil {
			return variant{}, err
		}
	}
	game, err := vcs.AddWorktree(root, dir, variantPrefix+name, from)
	if err != nil {
		return variant{}, err
	}
	// Its .jm ignores itself: a version from before the game ignored .jm would commit its plays.
	if err := os.MkdirAll(filepath.Join(game, ".jm"), 0o755); err != nil {
		return variant{}, err
	}
	if err := os.WriteFile(filepath.Join(game, ".jm", ".gitignore"), []byte("*\n"), 0o644); err != nil {
		return variant{}, err
	}
	return findVariant(root, name)
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
	return vcs.RemoveWorktree(root, filepath.Join(variantsDir(root), name), v.Branch)
}

func dropText(name string, kept bool) string {
	if kept {
		return fmt.Sprintf("Dropped variant %s; its commits stay on branch %s%s (git branch -D deletes it).", name, variantPrefix, name)
	}
	return "Dropped variant " + name + "."
}

// playGame opens a game in its own window for the person (built first if it needs it).
func playGame(dir string) error {
	if buildIsStale(dir) {
		if out, failed := jmIn(dir, "build", "--json"); failed {
			return fmt.Errorf("the build failed:\n%s", out)
		}
	}
	return startDetached(jmCommand(dir, "run"), "the game")
}

// comparison is two takes on the game: the files that differ (a scene's per
// entity) and a frame of each at the same moment ("" when it couldn't run).
type comparison struct {
	A, B    string       `json:"-"`
	Changes []fileChange `json:"changes"`
	Frame   int          `json:"frame"`
	Frames  [2]string    `json:"frames"` // image files
	Errors  [2]string    `json:"errors"`
}

func (c comparison) text() string {
	var b strings.Builder
	fmt.Fprintf(&b, "%s against %s: %d files differ.\n", c.A, c.B, len(c.Changes))
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
// their last commits change and uncommitted work aside, and a frame of each.
func compareVariants(root, a, b string, frame int, play string) (comparison, error) {
	cmp := comparison{A: a, B: b, Frame: max(frame, 1)}
	sides := [2]struct{ dir, rev string }{}
	for i, name := range []string{a, b} {
		if name == "game" {
			sides[i].dir, sides[i].rev = root, "HEAD"
			continue
		}
		v, err := findVariant(root, name)
		if err != nil {
			return cmp, err
		}
		sides[i].dir, sides[i].rev = v.Dir, "refs/heads/"+v.Branch
	}
	marks, err := vcs.Changed(root, sides[1].rev, sides[0].rev)
	if err != nil {
		return cmp, err
	}
	cmp.Changes = []fileChange{}
	for path, mark := range marks {
		c := fileChange{Path: path, Status: mark}
		if strings.HasSuffix(path, ".scene.json") {
			c.Entities = sceneChanges(vcs.Show(root, sides[1].rev, path), vcs.Show(root, sides[0].rev, path))
		}
		cmp.Changes = append(cmp.Changes, c)
	}
	sort.Slice(cmp.Changes, func(i, j int) bool { return cmp.Changes[i].Path < cmp.Changes[j].Path })
	var p *plays.Play
	if play != "" {
		if p, err = plays.Find(root, play); err != nil {
			return cmp, err
		}
	}
	work := filepath.Join(root, ".jm", "compare") // kept for a look; the next compare replaces them
	if err := os.MkdirAll(work, 0o755); err != nil {
		return cmp, err
	}
	for i, s := range sides {
		cmp.Frames[i], err = frameOf(s.dir, uint64(cmp.Frame), p, filepath.Join(work, []string{a, b}[i]+".png"))
		if err != nil {
			cmp.Errors[i] = err.Error()
		}
	}
	return cmp, nil
}

// frameOf runs a game headless (built first if it needs it) to a frame, with
// a play's inputs when given, and saves that frame as out.
func frameOf(dir string, frame uint64, p *plays.Play, out string) (string, error) {
	if _, err := resolveEnginePath(); err != nil { // before a build that would be for nothing
		return "", err
	}
	if buildIsStale(dir) {
		if text, failed := jmIn(dir, "build", "--json"); failed {
			return "", fmt.Errorf("the build failed: %s", clip(text, 400))
		}
	}
	o := gameOptions{GL: true, Seed: new(uint64)}
	if p != nil {
		o.Play, o.Until, o.Seed = p, frame+1, nil
	}
	g, err := startGame(dir, o)
	if err != nil {
		return "", err
	}
	defer g.close()
	if err := g.to(frame); err != nil {
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
