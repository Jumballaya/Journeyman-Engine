// Package gamelock lets one jm at a time change the person's games' files, so
// what it checked (a version, a free name, a note's text) still holds when the
// change lands.
package gamelock

import (
	"path/filepath"

	"github.com/Jumballaya/Journeyman-Engine/internal/filelock"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// Hold blocks until this process alone may change games' files, then gives
// back the release. One lock for every game (beside the editor's recent.json):
// games nest and link into each other, so one file can be several games'.
// Not reentrant: a holder mustn't Hold again.
func Hold() (release func(), err error) {
	return filelock.Hold(filepath.Join(filepath.Dir(projects.File()), "files"))
}
