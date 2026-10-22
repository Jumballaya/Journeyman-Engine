// Package copies is where a game keeps its copies (variants: a branch checked
// out beside it; sandboxes: its folder copied) and whose copy a folder is. A
// copy is its game's: what sits beside the game is found beside the game.
package copies

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/jmfolder"
)

// A Kind of copy.
type Kind string

const (
	Variant Kind = "variant" // <game>/.jm/variants/<name>: a worktree, the game where it sits in its repository
	Sandbox Kind = "sandbox" // <game>/.jm/sandboxes/<name>: the game's folder copied
)

// stores: each kind's folder in the game's .jm.
var stores = map[Kind]string{Variant: "variants", Sandbox: "sandboxes"}

// Store is the folder game keeps its copies of kind in, each named by its copy.
func Store(game string, kind Kind) string { return filepath.Join(game, ".jm", stores[kind]) }

// OpenStore is game's Store of kind, made when create (else "" when there's
// none); each step its own folder: a link is refused, never followed.
func OpenStore(game string, kind Kind, create bool) (string, error) {
	if create {
		if _, err := jmfolder.Make(game, stores[kind]); err != nil {
			return "", err
		}
	}
	dir := game
	for _, step := range []string{".jm", stores[kind]} {
		dir = filepath.Join(dir, step)
		info, err := os.Lstat(dir)
		switch {
		case errors.Is(err, fs.ErrNotExist) && !create:
			return "", nil
		case err != nil:
			return "", err
		case !info.IsDir():
			return "", fmt.Errorf("%s isn't a folder of the game's own (a link?): %ss are kept only there", dir, kind)
		}
	}
	return dir, nil
}

// A Copy is a folder's provenance: the game it's a copy of.
type Copy struct {
	Game string
	Kind Kind
	Name string
}

// Of is whose copy the game folder dir is; false for a game's own folder (or
// a game that only sits under a copy's path, or a copy's subfolder). The copy
// needn't be there still (a dropped one is its game's); its game must be.
func Of(dir string) (Copy, bool) {
	dir = filepath.Clean(dir)
	parts := strings.Split(filepath.ToSlash(dir), "/")
	for i := len(parts) - 3; i >= 1; i-- {
		if parts[i] != ".jm" {
			continue
		}
		for kind, store := range stores {
			if parts[i+1] != store {
				continue
			}
			game := filepath.FromSlash(strings.Join(parts[:i], "/"))
			if game == "" {
				game = string(filepath.Separator)
			}
			if c := (Copy{game, kind, parts[i+2]}); isGame(game) && dir == c.gameFolder() {
				return c, true
			}
		}
	}
	return Copy{}, false
}

// gameFolder is where c's game is: a variant's where the game sits in its repository.
func (c Copy) gameFolder() string {
	place := filepath.Join(Store(c.Game, c.Kind), c.Name)
	if c.Kind == Variant {
		return filepath.Join(place, inRepo(c.Game))
	}
	return place
}

// inRepo is where dir sits in its git repository ("games/pip"), "" at its top
// or outside one (vcs.GameIn's place; vcs needs the games' lock, which needs this).
func inRepo(dir string) string {
	c := exec.Command("git", "rev-parse", "--show-prefix")
	c.Dir = dir
	out, err := c.Output()
	if err != nil {
		return ""
	}
	return filepath.FromSlash(strings.TrimSuffix(strings.TrimSuffix(string(out), "\n"), "/"))
}

// Beside is where the game in dir finds ref, a path beside it ("../common", a
// script library): beside its game when dir is a copy. ref may be absolute.
func Beside(dir, ref string) string {
	if filepath.IsAbs(ref) {
		return filepath.Clean(ref)
	}
	if c, ok := Of(dir); ok && !filepath.IsLocal(ref) {
		return filepath.Join(c.Game, ref)
	}
	return filepath.Join(dir, ref)
}

func isGame(dir string) bool {
	info, err := os.Stat(filepath.Join(dir, archive.ManifestEntryKey))
	return err == nil && !info.IsDir()
}
