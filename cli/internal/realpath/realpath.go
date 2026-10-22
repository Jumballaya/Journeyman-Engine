// Package realpath says where a path really leads, and whether that's in a
// folder: links followed, a link to nothing refused, a case-blind disk's other
// spellings seen. What a caller allows there (hidden folders, links) is its own.
package realpath

import (
	"os"
	"path/filepath"
	"strings"
)

// Of is path (from the working folder, or absolute) cleaned, with its links
// resolved: through its nearest part that exists, the rest as written (a file
// not made yet). "" through a link to nothing: a write would land who knows where.
func Of(path string) string {
	path, err := filepath.Abs(path)
	if err != nil {
		return ""
	}
	rest := ""
	for p := path; ; p = filepath.Dir(p) {
		if _, err := os.Lstat(p); err == nil {
			real, err := filepath.EvalSymlinks(p)
			if err != nil {
				return ""
			}
			return filepath.Join(real, rest)
		}
		if filepath.Dir(p) == p {
			return path
		}
		rest = filepath.Join(filepath.Base(p), rest)
	}
}

// Rel is where path (from dir, unless absolute) really is, and its slash path
// from dir ("." for dir itself); false when it leads out of dir. A ".." is
// refused: users of a path clean it, and after a link it lands elsewhere.
func Rel(dir, path string) (real, rel string, ok bool) {
	if hasDotDot(path) || !filepath.IsAbs(path) && !filepath.IsLocal(path) { // IsLocal: no "C:x", NUL or ""
		return "", "", false
	}
	if !filepath.IsAbs(path) {
		path = filepath.Join(dir, path)
	}
	realDir, real := Of(dir), Of(path)
	if realDir == "" || real == "" {
		return "", "", false
	}
	if rel, err := filepath.Rel(realDir, real); err == nil && (rel == "." || filepath.IsLocal(rel)) {
		return real, filepath.ToSlash(rel), true
	}
	want, err := os.Stat(realDir) // dir spelled otherwise (Game, game): found by what it is
	if err != nil {
		return "", "", false
	}
	rest := "."
	for p := real; ; p = filepath.Dir(p) {
		if got, err := os.Stat(p); err == nil && os.SameFile(want, got) {
			if rest != "." && !filepath.IsLocal(rest) {
				return "", "", false
			}
			return real, filepath.ToSlash(rest), true
		}
		if filepath.Dir(p) == p {
			return "", "", false
		}
		rest = filepath.Join(filepath.Base(p), rest)
	}
}

// In says whether path (from dir, unless absolute) is dir or in it.
func In(dir, path string) bool {
	_, _, ok := Rel(dir, path)
	return ok
}

// hasDotDot says whether path has a ".." part, by either separator (a Windows path read on any disk).
func hasDotDot(path string) bool {
	for _, part := range strings.FieldsFunc(path, func(r rune) bool { return r == '/' || r == '\\' }) {
		if part == ".." {
			return true
		}
	}
	return false
}
