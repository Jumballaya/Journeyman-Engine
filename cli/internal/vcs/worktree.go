package vcs

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Branches beside the game's own, each checked out in its own folder (git
// worktrees): another take on the game to try, compare, and merge or drop.

// Branch is the branch the game is on ("" when detached or not in git).
func Branch(root string) string {
	out, err := git(root, "symbolic-ref", "--quiet", "--short", "HEAD")
	if err != nil {
		return ""
	}
	return strings.TrimSpace(out)
}

// AddWorktree checks out a new branch, made from version from ("": the last
// commit), at dir; the game's folder in it is returned (the same place in the
// repository as root's).
func AddWorktree(root, dir, branch, from string) (string, error) {
	hash, err := version(root, from)
	if err != nil {
		return "", err
	}
	prefix, _ := git(root, "rev-parse", "--show-prefix")
	if _, err := git(root, "cat-file", "-e", hash+":"+strings.TrimSuffix(prefix, "\n")+".jm.json"); err != nil {
		return "", fmt.Errorf("that version has no game here (it's from before the game was made)")
	}
	if _, err := git(root, "worktree", "add", "--quiet", "-b", branch, "--end-of-options", dir, hash); err != nil {
		return "", err
	}
	return GameIn(root, dir), nil
}

// GameIn is the game's folder in a worktree at dir: dir plus root's place in its repository.
func GameIn(root, dir string) string {
	prefix, _ := git(root, "rev-parse", "--show-prefix")
	return filepath.Join(dir, filepath.FromSlash(strings.TrimSuffix(prefix, "\n")))
}

// RemoveWorktree takes the worktree at dir away, its uncommitted changes
// with it; its branch is deleted too when it's merged, else kept (kept: true).
func RemoveWorktree(root, dir, branch string) (kept bool, err error) {
	if _, statErr := os.Stat(dir); statErr == nil {
		if _, err := git(root, "worktree", "remove", "--force", dir); err != nil {
			return false, err
		}
	}
	_, _ = git(root, "worktree", "prune") // a folder deleted by hand
	_, err = git(root, "branch", "-d", "--end-of-options", branch)
	return err != nil, nil // -d refuses an unmerged branch: it stays
}

// Ignored says whether git ignores path (from the game's folder).
func Ignored(root, path string) bool {
	_, err := git(root, "check-ignore", "-q", "--no-index", "--", path)
	return err == nil
}

// Distance is how many commits branch has that the game's branch doesn't
// (ahead), and the other way (behind).
func Distance(root, branch string) (ahead, behind int) {
	out, err := git(root, "rev-list", "--left-right", "--count", "--end-of-options", "refs/heads/"+branch+"...HEAD")
	if err != nil {
		return 0, 0
	}
	f := strings.Fields(out)
	if len(f) == 2 {
		ahead, _ = strconv.Atoi(f[0])
		behind, _ = strconv.Atoi(f[1])
	}
	return ahead, behind
}

// Changed is the game's files that differ between two versions (revs or
// branches), by Status's marks.
func Changed(root, a, b string) (map[string]string, error) {
	ha, err := version(root, a)
	if err != nil {
		return nil, err
	}
	hb, err := version(root, b)
	if err != nil {
		return nil, err
	}
	out, err := git(root, "diff", "--no-renames", "--relative", "--name-status", "-z", ha, hb, "--", ".")
	if err != nil {
		return nil, err
	}
	marks := map[string]string{}
	f := strings.Split(out, "\x00")
	for i := 0; i+1 < len(f); i += 2 {
		if f[i] != "" {
			marks[f[i+1]] = f[i][:1]
		}
	}
	return marks, nil
}

// Merge brings branch into the game's branch as a merge commit. One that
// fails (a conflict, a hook) is undone, nothing changed, conflicts named.
func Merge(root, branch, message string) (Version, error) {
	if _, err := version(root, "refs/heads/"+branch); err != nil {
		return Version{}, fmt.Errorf("no branch %s", branch)
	}
	if _, err := git(root, "rev-parse", "-q", "--verify", "MERGE_HEAD"); err == nil {
		return Version{}, fmt.Errorf("a merge is already under way in the game's repository: finish or abort it first")
	}
	// A game inside a bigger repository: the variant may change only the game.
	prefix, _ := git(root, "rev-parse", "--show-prefix")
	if prefix = strings.TrimSuffix(prefix, "\n"); prefix != "" {
		base, err := git(root, "merge-base", "HEAD", "refs/heads/"+branch)
		if err != nil {
			return Version{}, err
		}
		changed, err := git(root, "diff", "--name-only", "--no-renames", "--no-relative", "--ignore-submodules=none", "-z", strings.TrimSpace(base), "refs/heads/"+branch) // the whole repository's, from its top
		if err != nil {
			return Version{}, err
		}
		for _, p := range strings.Split(changed, "\x00") {
			if p != "" && !strings.HasPrefix(p, prefix) {
				return Version{}, fmt.Errorf("%s changes %s, outside the game: nothing was merged (merge it with git if that's meant)", branch, p)
			}
		}
	}
	before, _ := version(root, "HEAD")
	// No autostash (merge.autoStash): git then refuses local changes it would overwrite, instead of leaving conflict
	// markers. No renames: a rename on the game's side can't carry the variant's edit out of the game.
	_, err := git(root, append(identity(root), "-c", "merge.autoStash=false", "merge", "--no-ff", "-X", "no-renames", "--no-edit", "-m", message, "--end-of-options", "refs/heads/"+branch)...)
	if err != nil {
		conflicts, _ := git(root, "diff", "--name-only", "--diff-filter=U")
		if _, inMerge := git(root, "rev-parse", "-q", "--verify", "MERGE_HEAD"); inMerge == nil {
			_, _ = git(root, "merge", "--abort")
		}
		if names := strings.Fields(conflicts); len(names) > 0 {
			return Version{}, fmt.Errorf("it conflicts with the game's own changes in %s: nothing was merged; change one side so they agree, then merge again", strings.Join(names, ", "))
		}
		return Version{}, fmt.Errorf("nothing was merged: %v", err)
	}
	hash, _ := version(root, "HEAD")
	if hash == before {
		return Version{}, fmt.Errorf("the game already has everything %s has", branch)
	}
	// Not Log: a log of the game's paths leaves merge commits out.
	files, err := Changed(root, "HEAD^1", "HEAD")
	return Version{Hash: hash, Subject: message, Files: files}, err
}
