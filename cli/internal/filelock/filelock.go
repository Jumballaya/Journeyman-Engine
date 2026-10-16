// Package filelock holds a file for one process at a time, across jm and the
// editor (engine/core/app/Platform.hpp's FileLock locks the same way).
package filelock

import (
	"os"
	"path/filepath"
)

// Hold blocks until this process alone holds path, then gives back the
// release. The lock is a file beside it (".name.lock"), as path itself may be
// replaced while held.
func Hold(path string) (release func(), err error) {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return nil, err
	}
	f, err := os.OpenFile(filepath.Join(filepath.Dir(path), "."+filepath.Base(path)+".lock"), os.O_RDWR|os.O_CREATE, 0o644)
	if err != nil {
		return nil, err
	}
	if err := lock(f); err != nil {
		f.Close()
		return nil, err
	}
	return func() { f.Close() }, nil // closing unlocks it
}
