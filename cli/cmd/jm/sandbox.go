package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"sort"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"

	"github.com/spf13/cobra"
)

// Sandboxes: throwaway copies of the game as it is now (uncommitted changes
// too) in .jm/sandboxes, for an experiment that must never touch the game.
// Each expires; expired ones are cleared whenever sandboxes are looked at. One
// worth keeping becomes a variant.

var sandboxTTL, sandboxTimeout time.Duration

var sandboxCmd = &cobra.Command{
	Use:   "sandbox",
	Short: "Throwaway copies of the game for experiments: run jm there, keep it as a variant, or let it expire",
	Long: `A sandbox is a copy of the game as it is now, uncommitted changes included,
in .jm/sandboxes/<name>: build, test and play there without touching the
game. Each expires (default after a day) and is removed the next time
sandboxes are listed or made.

  jm sandbox                          the sandboxes, with what differs from the game (--json for tools)
  jm sandbox new [name] [--ttl 24h]   copy the game into a new one
  jm sandbox run <name> [--timeout 5m] -- <jm command...>
                                      run jm there, stopped at the timeout (e.g. -- build --json)
  jm sandbox promote <name> <variant> keep it: a variant (jm variant) with its changes, committed
  jm sandbox drop <name>              remove it now`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		return inGameRoot(func(root string) error {
			list, err := sandboxesOf(root)
			if err != nil {
				return err
			}
			if jsonOutput {
				return writeJSON(cmd.OutOrStdout(), map[string]any{"sandboxes": list})
			}
			if len(list) == 0 {
				fmt.Fprintln(cmd.OutOrStdout(), "No sandboxes: jm sandbox new makes one.")
			}
			for _, s := range list {
				fmt.Fprintf(cmd.OutOrStdout(), "%s  %s  (%s)\n", s.Name, s.Dir, s.summary())
			}
			return nil
		})
	},
}

func init() {
	sandboxCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	sub := func(use, short string, args cobra.PositionalArgs, run func(w io.Writer, root string, args []string) error) *cobra.Command {
		c := &cobra.Command{Use: use, Short: short, Args: args, RunE: func(cmd *cobra.Command, args []string) error {
			return inGameRoot(func(root string) error { return run(cmd.OutOrStdout(), root, args) })
		}}
		sandboxCmd.AddCommand(c)
		return c
	}
	n := sub("new [name]", "Copy the game into a new sandbox", cobra.MaximumNArgs(1), func(w io.Writer, root string, args []string) error {
		s, err := newSandbox(root, strings.Join(args, ""), sandboxTTL)
		if err == nil {
			fmt.Fprintf(w, "Made sandbox %s at %s (expires %s).\n", s.Name, s.Dir, time.Unix(s.Expires, 0).Format("2006-01-02 15:04"))
		}
		return err
	})
	n.Flags().DurationVar(&sandboxTTL, "ttl", 24*time.Hour, "how long it's kept")
	r := sub("run <name> -- <jm command...>", "Run a jm command in a sandbox", cobra.MinimumNArgs(2), func(w io.Writer, root string, args []string) error {
		out, err := runInSandbox(nil, root, args[0], args[1:], sandboxTimeout)
		fmt.Fprintln(w, out)
		return err
	})
	r.Flags().DurationVar(&sandboxTimeout, "timeout", 5*time.Minute, "stop it after this long")
	sub("promote <name> <variant>", "Keep a sandbox as a variant", cobra.ExactArgs(2), func(w io.Writer, root string, args []string) error {
		v, err := promoteSandbox(root, args[0], args[1])
		if err == nil {
			fmt.Fprintf(w, "Sandbox %s is variant %s now (%s).\n", args[0], v.Name, v.Dir)
		}
		return err
	})
	sub("drop <name>", "Remove a sandbox", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		err := dropSandbox(root, args[0])
		if err == nil {
			fmt.Fprintf(w, "Dropped sandbox %s.\n", args[0])
		}
		return err
	})
}

// sandbox is one sandbox: its folder, when it expires, and the files that
// differ from the game now (by Status's marks: M, A, D).
type sandbox struct {
	Name    string            `json:"name"`
	Dir     string            `json:"dir"`
	Made    int64             `json:"made"`
	Expires int64             `json:"expires"`
	Changes map[string]string `json:"changes"`
	thumb   string            // a picture of it, or ""
}

func (s sandbox) summary() string {
	return fmt.Sprintf("%d files differ from the game; expires %s", len(s.Changes), time.Unix(s.Expires, 0).Format("2006-01-02 15:04"))
}

// sandboxMeta is beside the sandbox's folder (not in it: it's the game's copy).
type sandboxMeta struct {
	Made    int64 `json:"made"`
	Expires int64 `json:"expires"`
}

// sandboxStore is the game's .jm/sandboxes, each step its own folder (a link
// is refused, never followed); "" when there's none and create is false.
func sandboxStore(root string, create bool) (string, error) {
	dir := root
	for _, step := range []string{".jm", "sandboxes"} {
		dir = filepath.Join(dir, step)
		info, err := os.Lstat(dir)
		if errors.Is(err, fs.ErrNotExist) && create {
			if err = os.Mkdir(dir, 0o755); err == nil {
				info, err = os.Lstat(dir)
			}
		}
		switch {
		case errors.Is(err, fs.ErrNotExist):
			return "", nil
		case err != nil:
			return "", err
		case !info.IsDir():
			return "", fmt.Errorf("%s isn't a folder of the game's own (a link?): sandboxes are kept only there", dir)
		}
	}
	return dir, nil
}

// sandboxesOf is the game's sandboxes, by name, the expired ones removed first.
func sandboxesOf(root string) ([]sandbox, error) {
	out := []sandbox{}
	store, err := sandboxStore(root, false)
	if err != nil || store == "" {
		return out, err
	}
	entries, _ := os.ReadDir(store)
	for _, e := range entries {
		name, ok := strings.CutSuffix(e.Name(), ".json")
		if !ok {
			continue
		}
		var m sandboxMeta
		data, _ := os.ReadFile(filepath.Join(store, e.Name()))
		dir := filepath.Join(store, name)
		if json.Unmarshal(data, &m) != nil || !ownFolder(dir) || time.Now().Unix() >= m.Expires {
			_ = dropSandbox(root, name)
			continue
		}
		out = append(out, sandbox{Name: name, Dir: dir, Made: m.Made, Expires: m.Expires, Changes: differences(root, dir), thumb: thumbOf(root, "sandbox", name, dir)})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out, nil
}

func findSandbox(root, name string) (sandbox, error) {
	list, err := sandboxesOf(root)
	if err != nil {
		return sandbox{}, err
	}
	for _, s := range list {
		if s.Name == name {
			return s, nil
		}
	}
	return sandbox{}, fmt.Errorf("no sandbox %q (jm sandbox lists them; it may have expired)", name)
}

// newSandbox copies the game's files (not build output, node_modules or
// dot-folders) into a new sandbox; name "": sb1, sb2…
func newSandbox(root, name string, ttl time.Duration) (sandbox, error) {
	existing, err := sandboxesOf(root) // clears the expired
	if err != nil {
		return sandbox{}, err
	}
	store, err := sandboxStore(root, true)
	if err != nil {
		return sandbox{}, err
	}
	if name == "" {
		for i := 1; ; i++ {
			if name = fmt.Sprintf("sb%d", i); !isDir(filepath.Join(store, name)) {
				break
			}
		}
	}
	if !variantName.MatchString(name) {
		return sandbox{}, fmt.Errorf("%q can't be a sandbox's name: lowercase letters, digits, - and _", name)
	}
	for _, s := range existing {
		if s.Name == name {
			return sandbox{}, fmt.Errorf("sandbox %s already exists", name)
		}
	}
	dir := filepath.Join(store, name)
	if err := os.Mkdir(dir, 0o755); err != nil { // this sandbox's own: anything already there (a link too) isn't taken over
		return sandbox{}, fmt.Errorf("sandbox %s can't be made: %v", name, err)
	}
	if err := syncFiles(root, dir); err != nil {
		_ = os.RemoveAll(dir)
		return sandbox{}, err
	}
	now := time.Now()
	data, _ := json.Marshal(sandboxMeta{Made: now.Unix(), Expires: now.Add(max(ttl, time.Minute)).Unix()})
	if err := os.WriteFile(filepath.Join(store, name+".json"), data, 0o644); err != nil {
		_ = os.RemoveAll(dir)
		return sandbox{}, err
	}
	return findSandbox(root, name)
}

// syncFiles makes to's game files those of from: copied over (a file, folder
// or link in the way replaced, never followed), then the ones from doesn't
// have removed. Build output, node_modules and dot-folders are left alone.
func syncFiles(from, to string) error {
	if !exists(filepath.Join(from, ".jm.json")) {
		return fmt.Errorf("%s isn't a game (it may have expired)", from)
	}
	if to == "" {
		return errors.New("no folder to copy the game to")
	}
	keep := map[string]bool{}
	var failed error
	err := walkProject(from, func(rel string, info fs.FileInfo) {
		keep[rel] = true
		data, err := os.ReadFile(filepath.Join(from, filepath.FromSlash(rel)))
		if err == nil {
			err = writeFresh(to, rel, data, info.Mode().Perm())
		}
		if err != nil && failed == nil {
			failed = fmt.Errorf("copying %s: %w", rel, err)
		}
	})
	if err == nil {
		err = failed
	}
	if err != nil {
		return fmt.Errorf("%v: nothing was removed or kept", err) // a copy missing files (one unread, too) prunes nothing
	}
	return walkProject(to, func(rel string, _ fs.FileInfo) {
		if !keep[rel] {
			_ = os.Remove(filepath.Join(to, filepath.FromSlash(rel)))
		}
	})
}

// writeFresh writes data as rel under root: each folder on the way that's a
// file or a link is replaced by a folder, and the file itself by a plain file.
func writeFresh(root, rel string, data []byte, perm fs.FileMode) error {
	if err := os.MkdirAll(root, 0o755); err != nil {
		return err
	}
	at := root
	parts := strings.Split(rel, "/")
	for _, part := range parts[:len(parts)-1] {
		at = filepath.Join(at, part)
		if info, err := os.Lstat(at); err == nil && !info.IsDir() {
			_ = os.Remove(at)
		}
		if err := os.Mkdir(at, 0o755); err != nil && !os.IsExist(err) {
			return err
		}
	}
	dst := filepath.Join(at, parts[len(parts)-1])
	if info, err := os.Lstat(dst); err == nil {
		if !info.Mode().IsRegular() {
			_ = os.RemoveAll(dst)
		} else if old, err := os.ReadFile(dst); err == nil && bytes.Equal(old, data) {
			return nil
		}
	}
	return os.WriteFile(dst, data, perm)
}

// differences is the files that differ between the game and a copy of it:
// M changed, A only in the copy, D only in the game.
func differences(game, copy string) map[string]string {
	sums := func(dir string) map[string]string {
		out := map[string]string{}
		_ = walkProject(dir, func(rel string, _ fs.FileInfo) {
			if data, err := os.ReadFile(filepath.Join(dir, filepath.FromSlash(rel))); err == nil {
				out[rel] = fileEtag(data)
			}
		})
		return out
	}
	a, b := sums(game), sums(copy)
	marks := map[string]string{}
	for rel, sum := range b {
		if was, ok := a[rel]; !ok {
			marks[rel] = "A"
		} else if was != sum {
			marks[rel] = "M"
		}
	}
	for rel := range a {
		if _, ok := b[rel]; !ok {
			marks[rel] = "D"
		}
	}
	return marks
}

// runInSandbox runs jm with args in the sandbox, stopped after timeout: its
// output, and an error when it failed or ran out of time.
func runInSandbox(c *toolCall, root, name string, args []string, timeout time.Duration) (string, error) {
	s, err := findSandbox(root, name)
	if err != nil {
		return "", err
	}
	if len(args) == 0 {
		return "", errors.New("give a jm command to run there, e.g. build --json or test --json")
	}
	// The command as jm runs it (flags anywhere): editor and a played variant outlive the run's time limit.
	switch path := strings.TrimPrefix(commandPath(args), "jm"); {
	case path == "" || strings.HasPrefix(path, " sandbox") || strings.HasPrefix(path, " mcp"):
		return "", errors.New("give a jm command to run there, e.g. build --json or test --json")
	case path == " editor" || path == " variant play":
		return "", fmt.Errorf("jm %s starts a program that outlives the run: open the sandbox's folder instead", strings.Join(args, " "))
	}
	for _, arg := range args { // the copy's files only: nothing outside it, links followed
		_, value, _ := strings.Cut(arg, "=")
		for _, p := range []string{arg, value} {
			up := slices.Contains(strings.FieldsFunc(p, func(r rune) bool { return r == '/' || r == '\\' }), "..") // "link/.." is the link's parent
			if p != "" && (filepath.IsAbs(p) || up || !inside(s.Dir, realOrNearest(filepath.Join(s.Dir, p)))) {
				return "", fmt.Errorf("%s leads out of the sandbox: give paths inside it", arg)
			}
		}
	}
	if err := linksStayIn(root, s.Dir); err != nil { // what a command finds on its own (fmt with no names) is the copy's too
		return "", err
	}
	ctx, cancel := context.WithTimeout(c.context(), max(timeout, time.Second))
	defer cancel()
	self, err := os.Executable()
	if err != nil {
		return "", err
	}
	run := exec.CommandContext(ctx, self, args...)
	run.Dir = s.Dir
	defer c.followEngineLog(s.Dir)()
	out, err := c.run(run) // the test runner or engine it started goes too
	text := clip(strings.TrimSpace(string(out)), 60000)
	if ctx.Err() == context.DeadlineExceeded {
		return text, fmt.Errorf("jm %s ran out of time (%s) in sandbox %s", strings.Join(args, " "), timeout, name)
	}
	return text, err
}

// promoteSandbox keeps a sandbox as a new variant: the variant (from the
// last commit) gets the sandbox's files, committed; the sandbox goes.
func promoteSandbox(root, name, as string) (variant, error) {
	s, err := findSandbox(root, name)
	if err != nil {
		return variant{}, err
	}
	v, err := newVariant(root, as, "")
	if err != nil {
		return variant{}, err
	}
	err = syncFiles(s.Dir, v.Dir)
	if err == nil {
		if _, err = vcs.Commit(v.Dir, "From sandbox "+name); errors.Is(err, vcs.ErrNothing) {
			err = nil
		}
	}
	if err != nil {
		_, _ = dropVariant(root, as) // nothing half-made left; the sandbox stays
		return variant{}, err
	}
	_ = dropSandbox(root, name)
	return findVariant(root, as)
}

// dropSandbox removes a sandbox; one that isn't there is already dropped.
func dropSandbox(root, name string) error {
	if !variantName.MatchString(name) {
		return fmt.Errorf("no sandbox %q", name)
	}
	store, err := sandboxStore(root, false)
	if err != nil || store == "" {
		return err
	}
	_ = os.Remove(filepath.Join(store, name+".json"))
	_ = os.Remove(compareImage(root, "sandbox", name))
	return os.RemoveAll(filepath.Join(store, name))
}

// commandPath is the command jm runs for args, e.g. "jm variant play"; set in
// init: jm's commands reach this through mcp, a cycle for a plain var.
var commandPath func(args []string) string

func init() {
	commandPath = func(args []string) string {
		cmd, _, _ := newRootCmd().Find(args)
		return cmd.CommandPath()
	}
}

// ownFolder says whether dir is a folder itself, not a link to one.
func ownFolder(dir string) bool {
	info, err := os.Lstat(dir)
	return err == nil && info.IsDir()
}

// linksStayIn refuses a sandbox holding a link that leads out of it or to
// nothing (a write through it would land elsewhere); its folder is the game's own.
func linksStayIn(root, dir string) error {
	if real, err := filepath.EvalSymlinks(dir); err != nil || !ownFolder(dir) || !inside(root, real) {
		return fmt.Errorf("sandbox %s isn't a folder of the game's own: drop it", filepath.Base(dir))
	}
	return filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.Type()&fs.ModeSymlink == 0 {
			return err
		}
		if real, err := filepath.EvalSymlinks(path); err != nil || !inside(dir, real) {
			rel, _ := filepath.Rel(dir, path)
			return fmt.Errorf("%s in the sandbox is a link out of it (or to nothing): remove it first", filepath.ToSlash(rel))
		}
		return nil
	})
}

// inside says whether path (real) is dir (real, once resolved) or in it.
func inside(dir, path string) bool {
	real, err := filepath.EvalSymlinks(dir)
	if err != nil {
		return false
	}
	rel, err := filepath.Rel(real, path)
	return err == nil && (rel == "." || filepath.IsLocal(rel))
}

// realOrNearest is path with links resolved: through its nearest part that
// exists, the rest as written (an output not made yet); "" through a dangling link.
func realOrNearest(path string) string {
	path = filepath.Clean(path)
	rest := ""
	for {
		if real, err := filepath.EvalSymlinks(path); err == nil {
			return filepath.Join(real, rest)
		}
		if _, err := os.Lstat(path); err == nil {
			return "" // a link to nothing: where a write through it lands is anyone's guess
		}
		parent := filepath.Dir(path)
		if parent == path {
			return filepath.Join(path, rest)
		}
		path, rest = parent, filepath.Join(filepath.Base(path), rest)
	}
}
