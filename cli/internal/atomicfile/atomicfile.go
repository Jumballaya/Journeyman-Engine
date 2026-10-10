// Package atomicfile writes files whole or not at all: a crash or a failed
// write leaves the old file (or none), never a truncated one.
package atomicfile

import (
	"bytes"
	"fmt"
	"io"
	"math/rand/v2"
	"os"
	"path/filepath"
)

// WriteFile replaces path with data, like os.WriteFile.
func WriteFile(path string, data []byte, perm os.FileMode) error {
	return Write(path, perm, func(w io.Writer) error {
		_, err := io.Copy(w, bytes.NewReader(data))
		return err
	})
}

// Write replaces path with what fill writes. fill writes to a temporary file
// beside path, renamed over it once complete; if fill fails, path is untouched.
// A symlink's target is replaced, and an existing file keeps its mode; a new
// one gets perm (less the umask).
func Write(path string, perm os.FileMode, fill func(w io.Writer) error) (err error) {
	path = followLinks(path)
	info, statErr := os.Stat(path)
	if statErr == nil {
		perm = info.Mode().Perm()
	}
	tmp, err := createBeside(path, perm)
	if err != nil {
		return err
	}
	defer func() {
		if err != nil {
			tmp.Close()
			os.Remove(tmp.Name())
		}
	}()
	if err = fill(tmp); err != nil {
		return err
	}
	if err = tmp.Sync(); err != nil {
		return err
	}
	if err = tmp.Close(); err != nil {
		return err
	}
	if statErr == nil { // the umask may have narrowed it
		if err = os.Chmod(tmp.Name(), perm); err != nil {
			return err
		}
	}
	return os.Rename(tmp.Name(), path) // replaces an existing file on Windows too
}

// followLinks is where a chain of symlinks at path ends, existing or not.
func followLinks(path string) string {
	for hops := 0; hops < 40; hops++ { // a loop: give up, like the OS does
		target, err := os.Readlink(path)
		if err != nil {
			return path
		}
		if !filepath.IsAbs(target) {
			target = filepath.Join(filepath.Dir(path), target)
		}
		path = target
	}
	return path
}

// createBeside makes a new hidden file in path's folder (skipped by folder scans).
func createBeside(path string, perm os.FileMode) (*os.File, error) {
	for tries := 0; ; tries++ {
		name := filepath.Join(filepath.Dir(path), fmt.Sprintf(".%s.tmp-%d", filepath.Base(path), rand.Uint32()))
		f, err := os.OpenFile(name, os.O_RDWR|os.O_CREATE|os.O_EXCL, perm)
		if !os.IsExist(err) || tries == 100 {
			return f, err
		}
	}
}
