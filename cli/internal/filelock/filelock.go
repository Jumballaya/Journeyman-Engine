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
func Hold(paths ...string) (release func(), err error) {
	var files []*os.File
	release = func() {
		for i := len(files) - 1; i >= 0; i-- {
			files[i].Close() // closing unlocks it
		}
	}
	for i, path := range paths {
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
		files = append(files, f)
	}
	for _, f := range files {
		if err := lock(f); err != nil {
			release()
			return nil, err
		}
	}
	return release, nil
}

// open opens path's lock file, made if need be.
func open(path string) (*os.File, error) {
	path, err := atomicfile.Target(path) // links to one file share its lock
	if err != nil {
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
	return os.OpenFile(filepath.Join(dir, "."+name+".lock"), os.O_RDWR|os.O_CREATE, 0o644)
}

func sameFile(a, b *os.File) bool {
	ia, errA := a.Stat()
	ib, errB := b.Stat()
	return errA == nil && errB == nil && os.SameFile(ia, ib)
}
