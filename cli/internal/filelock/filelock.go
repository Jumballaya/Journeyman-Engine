// Package filelock holds a file for one process at a time, across jm and the
// editor (engine/core/app/Platform.hpp's FileLock locks the same way).
package filelock

import (
	"os"
	"path/filepath"
	"runtime"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
)

// Hold blocks until this process alone holds path, then gives back the
// release. The lock is a file beside the file path leads to (".name.lock"), as
// that may be replaced while held.
func Hold(path string) (release func(), err error) {
	if path, err = atomicfile.Target(path); err != nil { // links to one file share its lock
		return nil, err
	}
	dir, name := filepath.Split(path)
	dir += "."
	if runtime.GOOS == "windows" {
		dir = filepath.Clean(dir) // Windows reads "alias\.." as text; POSIX resolves alias first
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, err
	}
	if real, err := filepath.EvalSymlinks(dir); err == nil {
		dir = real
	}
	f, err := os.OpenFile(filepath.Join(dir, "."+name+".lock"), os.O_RDWR|os.O_CREATE, 0o644)
	if err != nil {
		return nil, err
	}
	if err := lock(f); err != nil {
		f.Close()
		return nil, err
	}
	return func() { f.Close() }, nil // closing unlocks it
}
