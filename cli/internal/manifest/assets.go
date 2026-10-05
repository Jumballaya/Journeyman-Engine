package manifest

import (
	"io/fs"
	"path"
	"regexp"
	"sort"
	"strings"
)

// IsPattern reports whether a manifest asset entry is a glob: "*" matches
// within one path segment, "**" across segments, "?" one character.
func IsPattern(entry string) bool {
	return strings.ContainsAny(entry, "*?")
}

// ExpandAssets turns manifest asset entries into file paths: patterns become
// the files under `root` that match them (sorted), plain paths pass through.
// The result keeps first-seen order and has no duplicates. node_modules, build
// output and dot-files/directories (.DS_Store, .git) are never matched.
func ExpandAssets(root fs.FS, entries []string) ([]string, error) {
	var files []string
	walked := false
	seen := map[string]bool{}
	var out []string
	add := func(p string) {
		if !seen[p] {
			seen[p] = true
			out = append(out, p)
		}
	}
	for _, entry := range entries {
		if !IsPattern(entry) {
			add(entry)
			continue
		}
		if !walked {
			var err error
			if files, err = projectFiles(root); err != nil {
				return nil, err
			}
			walked = true
		}
		re := globRegexp(entry)
		var matched []string
		for _, f := range files {
			if re.MatchString(f) {
				matched = append(matched, f)
			}
		}
		sort.Strings(matched)
		for _, m := range matched {
			add(m)
		}
	}
	return out, nil
}

func projectFiles(root fs.FS) ([]string, error) {
	var files []string
	err := fs.WalkDir(root, ".", func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		name := d.Name()
		if d.IsDir() {
			if p != "." && (name == "node_modules" || name == "build" || name == "dist" || strings.HasPrefix(name, ".")) {
				return fs.SkipDir
			}
			return nil
		}
		if !strings.HasPrefix(name, ".") {
			files = append(files, p)
		}
		return nil
	})
	return files, err
}

func globRegexp(pattern string) *regexp.Regexp {
	pattern = path.Clean(strings.ReplaceAll(pattern, "\\", "/"))
	var b strings.Builder
	b.WriteString("^")
	for i := 0; i < len(pattern); i++ {
		c := pattern[i]
		switch {
		case strings.HasPrefix(pattern[i:], "**/"):
			b.WriteString("(?:.*/)?")
			i += 2
		case strings.HasPrefix(pattern[i:], "**"):
			b.WriteString(".*")
			i++
		case c == '*':
			b.WriteString("[^/]*")
		case c == '?':
			b.WriteString("[^/]")
		default:
			b.WriteString(regexp.QuoteMeta(string(c)))
		}
	}
	b.WriteString("$")
	return regexp.MustCompile(b.String())
}
