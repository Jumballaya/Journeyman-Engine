// Package vcs is a game's history in git as a person sees it: what changed,
// saving a version (a commit), the versions before, and going back to one.
// Everything is about the game's folder, even when it's part of a bigger
// repository; paths are from the game's folder, with slashes.
package vcs

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

// ErrNoGit: the game isn't in a git repository (Commit makes one), or there's no git.
var ErrNoGit = errors.New("the game has no history yet: a commit starts it")

// ErrNothing: a commit with nothing changed.
var ErrNothing = errors.New("nothing changed since the last commit")

// Status is git's mark for each changed file: M changed, A added, ? new to
// git, D deleted, U in conflict. Empty without a repository.
func Status(root string) map[string]string {
	marks := map[string]string{}
	prefix, err := git(root, "rev-parse", "--show-prefix")
	if err != nil {
		return marks
	}
	out, err := git(root, "status", "--porcelain=v1", "-z", "--untracked-files=all", "--no-renames", "--", ".")
	if err != nil {
		return marks
	}
	for _, line := range strings.Split(out, "\x00") {
		if len(line) < 4 {
			continue
		}
		if path, ok := strings.CutPrefix(line[3:], strings.TrimSuffix(prefix, "\n")); ok { // git's paths are from the repository's top
			marks[path] = mark(line[:2])
		}
	}
	return marks
}

// mark is one letter for git's two (index, worktree).
func mark(xy string) string {
	switch {
	case xy == "??":
		return "?"
	case strings.Contains(xy, "U"), xy == "AA", xy == "DD":
		return "U"
	case strings.Contains(xy, "D"):
		return "D"
	case xy[0] == 'A':
		return "A"
	}
	return "M"
}

// Init makes the game a repository with its files as the first commit, "New
// game"; a game already in one is left as it is.
func Init(root string) (bool, error) {
	if _, err := git(root, "rev-parse", "--is-inside-work-tree"); err == nil {
		return false, nil
	}
	_, err := Commit(root, "New game")
	return err == nil, err
}

// Version is one commit: the files it changed, by Status's marks.
type Version struct {
	Hash    string            `json:"hash"`
	Subject string            `json:"subject"`
	Author  string            `json:"author"`
	When    int64             `json:"when"` // unix seconds
	Files   map[string]string `json:"files"`
}

// Commit saves every change in the game as a version (message "": one
// naming what changed); ErrNothing when there's none. A game in no
// repository gets its own, and this is its first commit.
func Commit(root, message string) (Version, error) {
	if _, err := git(root, "rev-parse", "--is-inside-work-tree"); err != nil {
		if _, err := exec.LookPath("git"); err != nil {
			return Version{}, errors.New("git isn't installed: get it from git-scm.com")
		}
		if _, err := git(root, "init", "--quiet"); err != nil {
			return Version{}, err
		}
	}
	changes := Status(root)
	if len(changes) == 0 {
		return Version{}, ErrNothing
	}
	if strings.TrimSpace(message) == "" {
		message = summary(changes)
	}
	if _, err := git(root, "add", "-A", "--", "."); err != nil {
		return Version{}, err
	}
	// In a bigger repository, only the game's paths: what else is staged stays
	// staged. (A pathspec commit can't take a file that became a folder: not when the game is the repository.)
	args := append(identity(root), "commit", "--quiet", "--no-verify", "-m", message)
	if prefix, _ := git(root, "rev-parse", "--show-prefix"); strings.TrimSpace(prefix) != "" {
		args = append(args, "--", ".")
	}
	if _, err := git(root, args...); err != nil {
		return Version{}, err
	}
	log, err := Log(root, 1)
	if err != nil || len(log) == 0 {
		return Version{}, fmt.Errorf("committed, but couldn't read it back: %v", err)
	}
	return log[0], nil
}

// summary is a commit message from what changed: "Change main.scene.json, player.ts and 3 more".
func summary(changes map[string]string) string {
	var names []string
	for path := range changes {
		names = append(names, filepath.Base(path))
	}
	sort.Strings(names)
	if len(names) > 3 {
		return fmt.Sprintf("Change %s and %d more", strings.Join(names[:3], ", "), len(names)-3)
	}
	return "Change " + strings.Join(names, ", ")
}

// identity lets a commit be made on a machine where git was never told who
// the person is (as with ChatGPT's users), without changing git's settings.
func identity(root string) []string {
	if email, _ := git(root, "config", "user.email"); strings.TrimSpace(email) != "" {
		return nil
	}
	return []string{"-c", "user.name=Journeyman", "-c", "user.email=journeyman@localhost"}
}

// Log is the game's last n versions, newest first; none before the first commit.
func Log(root string, n int) ([]Version, error) {
	if _, err := git(root, "rev-parse", "--is-inside-work-tree"); err != nil {
		return nil, ErrNoGit
	}
	if _, err := git(root, "rev-parse", "--verify", "-q", "HEAD"); err != nil {
		return []Version{}, nil
	}
	prefix, _ := git(root, "rev-parse", "--show-prefix")
	prefix = strings.TrimSuffix(prefix, "\n")
	out, err := git(root, "log", "-n", strconv.Itoa(max(n, 1)), "--no-renames", "--no-relative", "--name-status", "-z", "--format=%x01%H%x00%s%x00%an%x00%at", "--", ".")
	if err != nil {
		return nil, err
	}
	versions := []Version{}
	for _, record := range strings.Split(out, "\x01")[1:] {
		fields := strings.Split(strings.TrimLeft(record, "\n"), "\x00")
		if len(fields) < 4 {
			continue
		}
		when, _ := strconv.ParseInt(strings.TrimSpace(fields[3]), 10, 64)
		v := Version{Hash: fields[0], Subject: fields[1], Author: fields[2], When: when, Files: map[string]string{}}
		rest := fields[4:]
		for i := 0; i+1 < len(rest); i += 2 { // status, path pairs
			status, path := strings.TrimSpace(rest[i]), rest[i+1]
			if p, ok := strings.CutPrefix(path, prefix); ok && status != "" {
				v.Files[p] = status[:1]
			}
		}
		versions = append(versions, v)
	}
	return versions, nil
}

// Show is a file as it was at rev (a version's hash, "HEAD"…); nil when it wasn't there.
func Show(root, rev, path string) []byte {
	hash, err := version(root, rev)
	if err != nil || !local(path) {
		return nil
	}
	out, err := gitBytes(root, "show", hash+":./"+path)
	if err != nil {
		return nil
	}
	return out
}

// version is the commit rev names ("": the last), as its hash: never read as an option.
func version(root, rev string) (string, error) {
	if rev == "" {
		rev = "HEAD"
	}
	hash, err := git(root, "rev-parse", "--verify", "-q", "--end-of-options", rev+"^{commit}")
	if err != nil || strings.HasPrefix(rev, "-") {
		return "", fmt.Errorf("no version %q (jm vcs log lists them)", rev)
	}
	return strings.TrimSpace(hash), nil
}

// local: a path in the game (from its folder), not one leading out of it.
func local(path string) bool {
	return path == "." || filepath.IsLocal(filepath.FromSlash(path))
}

// Diff is git's line diff of a file (path "": the whole game) since rev
// ("": the last commit), files new to git included.
func Diff(root, rev, path string) (string, error) {
	hash, err := version(root, rev)
	if err != nil {
		return "", err
	}
	spec := "."
	if path != "" {
		spec = path
	}
	if !local(spec) {
		return "", fmt.Errorf("%s isn't in the game", path)
	}
	out, err := git(root, "diff", "--no-color", "--no-ext-diff", hash, "--", spec)
	if err != nil {
		return "", err
	}
	for p, m := range Status(root) {
		if m == "?" && (path == "" || p == path || strings.HasPrefix(p, strings.TrimSuffix(path, "/")+"/")) {
			added, _ := git(root, "diff", "--no-color", "--no-index", "--", os.DevNull, p) // exits 1 when they differ: still the diff
			out += added
		}
	}
	return out, nil
}

// Restore puts paths ("." for the whole game) back as they were at rev ("":
// the last commit). Changes it would lose go to git's stash first; backup
// says so ("" when there were none).
func Restore(root, rev string, paths []string) (backup string, err error) {
	if len(paths) == 0 {
		return "", errors.New("say what to restore: files, or . for the whole game")
	}
	for _, p := range paths {
		if !local(p) {
			return "", fmt.Errorf("%s isn't in the game", p)
		}
	}
	hash, err := version(root, rev)
	if err != nil {
		return "", err
	}
	// The stash leaves out ignored files: one the version would write over isn't restored.
	tracked, _ := git(root, append([]string{"ls-tree", "-r", "--name-only", hash, "--"}, paths...)...)
	ignored, _ := git(root, append([]string{"ls-files", "--others", "--ignored", "--exclude-standard", "--"}, paths...)...)
	if clash := both(tracked, ignored); len(clash) > 0 {
		return "", fmt.Errorf("that version has files git now ignores and you have (%s): move them away first", strings.Join(clash, ", "))
	}
	before, _ := git(root, "rev-parse", "-q", "--verify", "refs/stash")
	if _, err := git(root, append(identity(root), append([]string{"stash", "push", "--include-untracked", "--quiet", "-m", "jm vcs restore: changes before going back to " + hash[:8], "--"}, paths...)...)...); err != nil {
		return "", err
	}
	stashed := false
	if after, _ := git(root, "rev-parse", "-q", "--verify", "refs/stash"); after != before {
		stashed = true
		backup = "your changes are kept in git's stash (git stash list; git stash pop brings them back)"
	}
	if _, err := git(root, append([]string{"restore", "--source", hash, "--staged", "--worktree", "--"}, paths...)...); err != nil {
		if stashed {
			_, _ = git(root, "stash", "pop", "--quiet", "--index") // nothing restored: put the changes back
		}
		return "", err
	}
	return backup, nil
}

// both is the lines in both listings.
func both(a, b string) []string {
	in := map[string]bool{}
	for _, p := range strings.Split(a, "\n") {
		in[p] = p != ""
	}
	var out []string
	for _, p := range strings.Split(b, "\n") {
		if in[p] {
			out = append(out, p)
		}
	}
	return out
}

func git(root string, args ...string) (string, error) {
	out, err := gitBytes(root, args...)
	return string(out), err
}

func gitBytes(root string, args ...string) ([]byte, error) {
	cmd := exec.Command("git", append([]string{"-C", root}, args...)...)
	// Literal pathspecs: a path like ":(top)" can't reach past the game.
	cmd.Env = append(os.Environ(), "GIT_TERMINAL_PROMPT=0", "GIT_OPTIONAL_LOCKS=0", "GIT_LITERAL_PATHSPECS=1")
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	out, err := cmd.Output()
	if msg := strings.TrimSpace(stderr.String()); err != nil && msg != "" {
		return out, errors.New(msg)
	}
	return out, err
}
