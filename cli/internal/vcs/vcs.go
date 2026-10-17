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
	"path"
	"path/filepath"
	"slices"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/gamelock"
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
	// Only the game's paths: what else is staged in a bigger repository stays staged.
	if _, err := git(root, append(identity(root), "commit", "--quiet", "--no-verify", "-m", message, "--", ".")...); err != nil {
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
	hashes, err := git(root, "log", "-z", "-n", strconv.Itoa(max(n, 1)), "--format=%H", "--", ".")
	if err != nil {
		return nil, err
	}
	versions := []Version{}
	for _, hash := range strings.Split(strings.Trim(hashes, "\x00\n"), "\x00") {
		hash = strings.TrimSpace(hash)
		if hash == "" {
			continue
		}
		v, err := readVersion(root, hash, prefix)
		if err != nil {
			return nil, err
		}
		versions = append(versions, v)
	}
	return versions, nil
}

// readVersion is one commit, asked for field by field: a subject, an author
// or a path may hold any byte but NUL, so nothing else can frame them.
func readVersion(root, hash, prefix string) (Version, error) {
	if !fullHash(hash) {
		return Version{}, fmt.Errorf("git gave %q for a commit", hash)
	}
	v := Version{Hash: hash, Files: map[string]string{}}
	for field, into := range map[string]*string{"%s": &v.Subject, "%an": &v.Author} {
		out, err := git(root, "show", "-s", "--format="+field, hash)
		if err != nil {
			return Version{}, err
		}
		*into = strings.TrimSuffix(out, "\n")
	}
	when, err := git(root, "show", "-s", "--format=%at", hash)
	if err != nil {
		return Version{}, err
	}
	v.When, _ = strconv.ParseInt(strings.TrimSpace(when), 10, 64)
	files, err := git(root, "diff-tree", "-r", "-z", "--root", "--no-commit-id", "--no-renames", "--no-relative", "--name-status", hash, "--", ".")
	if err != nil {
		return Version{}, err
	}
	rest := strings.Split(strings.TrimSuffix(files, "\x00"), "\x00")
	for i := 0; i+1 < len(rest); i += 2 { // status, path pairs
		status, path := rest[i], rest[i+1]
		if p, ok := strings.CutPrefix(path, prefix); ok && status != "" {
			v.Files[p] = status[:1]
		}
	}
	return v, nil
}

// fullHash: a whole SHA-1 or SHA-256 commit name.
func fullHash(s string) bool {
	if len(s) != 40 && len(s) != 64 {
		return false
	}
	for _, c := range s {
		if !strings.ContainsRune("0123456789abcdef", c) {
			return false
		}
	}
	return true
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
	if path != "" {
		path = filepath.ToSlash(filepath.Clean(path)) // "./a", "a/", "a/../a": a
	}
	if path == "." {
		path = "" // the whole game
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
		if m == "?" && (path == "" || p == path || strings.HasPrefix(p, path+"/")) {
			added, _ := git(root, "diff", "--no-color", "--no-index", "--", os.DevNull, p) // exits 1 when they differ: still the diff
			out += added
		}
	}
	return out, nil
}

// Restore puts paths ("." for the whole game) back as they were at rev ("":
// the last commit). Changes it would lose go to git's stash first; backup
// says so ("" when there were none), also with an error: the stash is kept
// whatever happened, and never dropped here. It holds the games' lock throughout, so a save can't land
// between the backup and the restore and be lost.
func Restore(root, rev string, paths []string) (backup string, err error) {
	if len(paths) == 0 {
		return "", errors.New("say what to restore: files, or . for the whole game")
	}
	for _, p := range paths {
		if !local(p) {
			return "", fmt.Errorf("%s isn't in the game", p)
		}
	}
	release, err := gamelock.Hold()
	if err != nil {
		return "", err
	}
	defer release()
	hash, err := version(root, rev)
	if err != nil {
		return "", err
	}
	// The stash leaves out ignored files: one the version would write over (or
	// make a folder or a file of) isn't restored.
	tracked, err := git(root, append([]string{"ls-tree", "-r", "-z", "--name-only", hash, "--"}, paths...)...)
	if err != nil {
		return "", err
	}
	ignored, err := git(root, "ls-files", "-z", "--others", "--ignored", "--exclude-standard") // all the game's: "dir" can be in dir/a's way
	if err != nil {
		return "", err
	}
	if clash := inTheWay(tracked, ignored); len(clash) > 0 {
		return "", fmt.Errorf("that version has files git now ignores and you have (%s): move them away first", strings.Join(clash, ", "))
	}
	// git stash reads literal paths from the repository's top, not the game's folder (it may be inside a bigger one).
	top, err := git(root, "rev-parse", "--show-toplevel")
	if err != nil {
		return "", err
	}
	top = strings.TrimSuffix(top, "\n")
	prefix, _ := git(root, "rev-parse", "--show-prefix")
	fromTop := make([]string, len(paths))
	for i, p := range paths {
		fromTop[i] = path.Clean(strings.TrimSuffix(prefix, "\n") + filepath.ToSlash(p)) // only git's newline: a folder may start with a space
	}
	// Our stash is the one with this tag: another git may push its own meanwhile.
	tag := fmt.Sprintf("jm vcs restore %d-%d", os.Getpid(), time.Now().UnixNano())
	if _, err := git(top, append(identity(root), append([]string{"stash", "push", "--include-untracked", "--quiet", "-m", tag + ": changes before going back to " + hash[:8], "--"}, fromTop...)...)...); err != nil {
		return "", err
	}
	stash, err := stashTagged(top, tag)
	if err != nil {
		backup = fmt.Sprintf("your changes are kept in git's stash, named %q (git stash list finds it)", tag)
		return backup, fmt.Errorf("nothing restored: your changes went to git's stash, but reading it back failed (%v)", err)
	}
	var steps [][]string
	if stash != "" {
		// Worked out now, while the index is as the stash left it.
		if steps, err = rollback(top, stash, hash, fromTop); err != nil {
			backup = fmt.Sprintf("your changes are kept in git's stash as %s", stash)
			return backup, fmt.Errorf("nothing restored: your changes went to git's stash, but reading it back failed (%v)", err)
		}
		backup = fmt.Sprintf("your changes are kept in git's stash as %s (%s)", stash[:8], recovery(steps))
	}
	if _, err := git(root, append([]string{"restore", "--source", hash, "--staged", "--worktree", "--"}, paths...)...); err != nil {
		if stash == "" {
			return "", err
		}
		// Nothing restored: put the changes back. The stash stays (a copy, never dropped: another's could be on top by now).
		for _, step := range steps {
			if _, undo := git(top, step...); undo != nil {
				return backup, fmt.Errorf("%v; putting your changes back failed too (%v): %s", err, undo, backup)
			}
		}
		return backup, fmt.Errorf("%v (your changes are back as they were)", err)
	}
	return backup, nil
}

// rollback is the git commands that put back what stash holds of the paths
// (from the repository's top), from before or after going back to version:
// the stash's tracked files, then no other tracked ones, its index, no other
// index entries, then its untracked files. Each names exactly the files it
// has, or ignores the ones that aren't there, so it works from either. Only
// the paths': git stash apply would replay the whole repository's index, and
// fail on what's staged outside the game.
func rollback(top, stash, version string, fromTop []string) ([][]string, error) {
	files := func(args ...string) ([]string, error) {
		out, err := git(top, append(args, fromTop...)...)
		names := slices.DeleteFunc(strings.Split(out, "\x00"), func(n string) bool { return n == "" })
		return names, err
	}
	tree := func(rev string) ([]string, error) {
		if _, err := git(top, "rev-parse", "-q", "--verify", rev+"^{tree}"); err != nil {
			return nil, nil // no untracked files were stashed
		}
		return files("ls-tree", "-r", "-z", "--name-only", rev, "--")
	}
	worktree, err := tree(stash)
	if err != nil {
		return nil, err
	}
	index, err := tree(stash + "^2")
	if err != nil {
		return nil, err
	}
	untracked, err := tree(stash + "^3")
	if err != nil {
		return nil, err
	}
	known, err := files("ls-files", "-z", "--") // the index now: the stash took the paths back to the last commit
	if err != nil {
		return nil, err
	}
	restored, err := tree(version)
	if err != nil {
		return nil, err
	}
	// A file in one version can be a folder in the index now: git rm of that
	// name would reach the files in it, so it's left out (they're named).
	folders := map[string]bool{}
	for _, f := range known {
		for d := path.Dir(f); d != "."; d = path.Dir(d) {
			folders[d] = true
		}
	}
	known = append(known, restored...)
	without := func(all []string, keep ...[]string) []string {
		var out []string
		for _, f := range all {
			if !folders[f] && !slices.ContainsFunc(keep, func(k []string) bool { return slices.Contains(k, f) }) {
				out = append(out, f)
			}
		}
		return slices.Compact(slices.Sorted(slices.Values(out)))
	}
	var steps [][]string
	add := func(names []string, cmd ...string) {
		if len(names) > 0 {
			steps = append(steps, append(append(cmd, "--"), names...))
		}
	}
	add(worktree, "restore", "--source", stash, "--worktree")
	add(without(known, worktree, untracked), "rm", "-q", "-f", "--ignore-unmatch")
	add(index, "restore", "--source", stash+"^2", "--staged")
	add(without(known, index), "rm", "-q", "--cached", "--ignore-unmatch")
	add(untracked, "restore", "--source", stash+"^3", "--worktree")
	return steps, nil
}

// recovery is rollback's restores as a person runs them in a shell at the
// repository's top: paths in single quotes, matched literally.
func recovery(steps [][]string) string {
	lines := make([]string, len(steps))
	for i, step := range steps {
		words := make([]string, len(step))
		for j, w := range step {
			words[j] = "'" + strings.ReplaceAll(w, "'", `'\''`) + "'"
		}
		lines[i] = "git --literal-pathspecs " + strings.Join(words, " ")
	}
	return "to bring them back, at the repository's top: " + strings.Join(lines, "; ")
}

// stashTagged is the stash whose message has tag ("" when none: there was nothing to stash).
func stashTagged(top, tag string) (string, error) {
	list, err := git(top, "stash", "list", "-z", "--format=%H%x00%gs")
	if err != nil {
		return "", err
	}
	fields := strings.Split(list, "\x00")
	for i := 0; i+1 < len(fields); i += 2 {
		if hash := strings.TrimLeft(fields[i], "\n"); strings.Contains(fields[i+1], tag+":") && fullHash(hash) {
			return hash, nil
		}
	}
	return "", nil
}

// inTheWay is the ignored paths (NUL-separated listings) a restore of the
// tracked ones would replace: the same path, one inside a tracked file, or one
// a tracked file's folder would need.
func inTheWay(tracked, ignored string) []string {
	files, folders := map[string]bool{}, map[string]bool{}
	for _, p := range strings.Split(tracked, "\x00") {
		if p == "" {
			continue
		}
		files[p] = true
		for d := path.Dir(p); d != "."; d = path.Dir(d) {
			folders[d] = true
		}
	}
	var out []string
	for _, p := range strings.Split(ignored, "\x00") {
		clash := files[p] || folders[p]
		for d := path.Dir(p); d != "." && !clash; d = path.Dir(d) {
			clash = files[d]
		}
		if p != "" && clash {
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
