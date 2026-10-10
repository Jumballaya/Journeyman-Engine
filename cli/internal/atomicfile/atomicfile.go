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
	"runtime"
	"strings"
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
	path, err = followLinks(path)
	if err != nil {
		return err
	}
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
func followLinks(path string) (string, error) {
	for hops := 0; ; hops++ {
		target, err := os.Readlink(path)
		if err != nil {
			return path, nil
		}
		if hops == 40 { // a loop: give up, like the OS does
			return "", fmt.Errorf("%s: too many levels of symbolic links", path)
		}
		if !filepath.IsAbs(target) {
			// Relative to the link's real folder; not Join, which drops "alias/.." before the OS resolves alias.
			dir, _ := filepath.Split(path)
			if real, err := filepath.EvalSymlinks(nativeDir(dir + ".")); err == nil {
				dir = strings.TrimSuffix(real, string(filepath.Separator)) + string(filepath.Separator) // a root keeps one
			}
			target = dir + target
		}
		path = target
	}
}

// nativeDir is dir as the OS reads it: Windows drops "alias\.." as text, but
// EvalSymlinks resolves alias first, as POSIX does.
func nativeDir(dir string) string {
	if runtime.GOOS == "windows" {
		return filepath.Clean(dir)
	}
	return dir
}

// createBeside makes a new hidden file in path's folder (skipped by folder scans).
func createBeside(path string, perm os.FileMode) (*os.File, error) {
	for tries := 0; ; tries++ {
		dir, base := filepath.Split(path)
		name := dir + fmt.Sprintf(".%s.tmp-%d", base, rand.Uint32())
		f, err := os.OpenFile(name, os.O_RDWR|os.O_CREATE|os.O_EXCL, perm)
		if !os.IsExist(err) || tries == 100 {
			return f, err
		}
	}
}
