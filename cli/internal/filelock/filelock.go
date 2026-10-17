// Package filelock holds a file for one process at a time, across jm and the
// editor (engine/core/app/Platform.hpp's FileLock locks the same way).
package filelock

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
)

// ErrOneFile: two paths Hold was given lead to one file, whose lock it would wait on itself for.
var ErrOneFile = errors.New("one file")

// Hold blocks until this process alone holds every path, taken in order, then
// gives back the release. Each lock is a file beside the file a path leads to
// (".name.lock"), as that may be replaced while held.
//
// A lock file can itself be replaced, moved or deleted (a game's files can
// hold the settings folder): Hold takes a lock only while it is still the file
// its path names, else it lets go and takes the one there now. So a lock left
// behind holds nothing, and waiters on it move on to the current one. A holder
// that replaces its own lock lets a newcomer in early, so a holder's change to
// files is its last step.
func Hold(paths ...string) (release func(), err error) {
	var files []*os.File
	release = func() {
		for i := len(files) - 1; i >= 0; i-- {
			files[i].Close() // closing unlocks it
		}
	}
	for i, path := range paths {
		for {
			f, err := open(path)
			if err != nil {
				release()
				return nil, err
			}
			for j, held := range files {
				if sameFile(held, f) {
					f.Close()
					release()
					return nil, fmt.Errorf("%s and %s: %w", paths[j], paths[i], ErrOneFile)
				}
			}
			if err := lock(f); err != nil {
				f.Close()
				release()
				return nil, err
			}
			if current(f, path) {
				files = append(files, f)
				break
			}
			f.Close() // replaced while we waited: take the one there now
		}
	}
	return release, nil
}

// current says whether f is still the lock file path's lock is at.
func current(f *os.File, path string) bool {
	name, err := lockFile(path)
	if err != nil {
		return false
	}
	now, err := os.Stat(name)
	held, heldErr := f.Stat()
	return err == nil && heldErr == nil && os.SameFile(now, held)
}

// open opens path's lock file, made if need be.
func open(path string) (*os.File, error) {
	dir, name, err := lockPlace(path)
	if err != nil {
		return nil, err
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, err
	}
	if real, err := filepath.EvalSymlinks(dir); err == nil {
		dir = real
	}
	return os.OpenFile(filepath.Join(dir, name), os.O_RDWR|os.O_CREATE, 0o644)
}

// lockFile is the lock file Hold takes for path (it may not exist yet).
func lockFile(path string) (string, error) {
	dir, name, err := lockPlace(path)
	if err != nil {
		return "", err
	}
	return dir + string(filepath.Separator) + name, nil
}

// lockPlace is the folder and name of path's lock file: beside the file path
// leads to, so links to one file share its lock.
func lockPlace(path string) (dir, name string, err error) {
	path, err = atomicfile.Target(path)
	if err != nil {
		return "", "", err
	}
	dir, name = filepath.Split(path)
	dir += "."
	if runtime.GOOS == "windows" {
		dir = filepath.Clean(dir) // Windows reads "alias\.." as text; POSIX resolves alias first
	}
	return dir, "." + name + ".lock", nil
}

func sameFile(a, b *os.File) bool {
	ia, errA := a.Stat()
	ib, errB := b.Stat()
	return errA == nil && errB == nil && os.SameFile(ia, ib)
}
