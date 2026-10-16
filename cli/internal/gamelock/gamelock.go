// Package gamelock holds a game's files for one jm at a time while it changes
// them, so what it checked (a version, a free name, a note's text) still holds.
package gamelock

import (
	"path/filepath"

	"github.com/Jumballaya/Journeyman-Engine/internal/filelock"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// Hold blocks until this process alone holds the game in root, then gives
// back the release. Not reentrant: a holder mustn't Hold again.
func Hold(root string) (release func(), err error) {
	if err := plays.MakeJMFolder(root); err != nil {
		return nil, err
	}
	return filelock.Hold(filepath.Join(root, ".jm", "files"))
}
