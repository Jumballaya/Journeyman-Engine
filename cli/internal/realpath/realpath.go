// Package realpath says where a path really leads, and whether that's in a
// folder: links followed, a link to nothing refused, a case-blind disk's other
// spellings seen. What a caller allows there (hidden folders, links) is its own.
package realpath

import (
	"os"
	"path/filepath"
)

// Of is path (from the working folder, or absolute) with its links resolved:
// through its nearest part that exists, the rest as written (a file not made
// yet). "" through a link to nothing, or a ".." past what exists: where a write
// would land is anyone's guess.
func Of(path string) string {
	if !filepath.IsAbs(path) {
		wd, err := os.Getwd()
		if err != nil {
			return ""
		}
		path = wd + string(filepath.Separator) + path // not Join: it'd clean "link/.." away
	}
	rest := ""
	for p := path; ; {
		if real, err := filepath.EvalSymlinks(p); err == nil { // a ".." after a link: from where it leads
			return filepath.Join(real, rest)
		}
		if _, err := os.Lstat(p); err == nil {
			return ""
		}
		dir, base, ok := parent(p)
		if !ok || base == ".." {
			return ""
		}
		if base != "." {
			rest = filepath.Join(base, rest)
		}
		p = dir
	}
}

// parent is p's folder and last part, cut as written (Dir cleans); false for the root.
func parent(p string) (string, string, bool) {
	vol := len(filepath.VolumeName(p))
	end := len(p)
	for end > vol+1 && os.IsPathSeparator(p[end-1]) {
		end--
	}
	i := end - 1
	for i >= vol && !os.IsPathSeparator(p[i]) {
		i--
	}
	if i < vol || i == end-1 {
		return "", "", false
	}
	if i == vol { // in the root
		return p[:vol+1], p[i+1 : end], true
	}
	return p[:i], p[i+1 : end], true
}

// Rel is path's slash path from dir ("." for dir itself), both as they really
// are; path is from dir unless absolute. False when it leads out of dir.
func Rel(dir, path string) (string, bool) {
	if !filepath.IsAbs(path) {
		path = dir + string(filepath.Separator) + path
	}
	realDir, real := Of(dir), Of(path)
	if realDir == "" || real == "" {
		return "", false
	}
	if rel, err := filepath.Rel(realDir, real); err == nil && (rel == "." || filepath.IsLocal(rel)) {
		return filepath.ToSlash(rel), true
	}
	want, err := os.Stat(realDir) // dir spelled otherwise (Game, game): found by what it is
	if err != nil {
		return "", false
	}
	rest := "."
	for p := real; ; p = filepath.Dir(p) {
		if got, err := os.Stat(p); err == nil && os.SameFile(want, got) {
			return filepath.ToSlash(rest), true
		}
		if filepath.Dir(p) == p {
			return "", false
		}
		rest = filepath.Join(filepath.Base(p), rest)
	}
}

// In says whether path (from dir unless absolute) is dir or in it.
func In(dir, path string) bool {
	_, ok := Rel(dir, path)
	return ok
}
