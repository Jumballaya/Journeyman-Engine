package manifest

import (
	"io/fs"
	"path"
	"regexp"
	"slices"
	"sort"
	"strings"
)

// ExpandAssets turns manifest asset entries into file paths: globs ("*"
// within a path segment, "**" across segments, "?" one character) become the
// files under `root` that match them (sorted), plain paths pass through.
// The result keeps first-seen order and has no duplicates. node_modules, build
// output, dot-files/directories (.DS_Store, .git) and the scripts' npm and
// compiler files (ScriptToolingFiles) are never matched.
func ExpandAssets(root fs.FS, entries []string) ([]string, error) {
	var files []string // walked on the first glob
	seen := map[string]bool{}
	var out []string
	add := func(p string) {
		if !seen[p] {
			seen[p] = true
			out = append(out, p)
		}
	}
	for _, entry := range entries {
		if !strings.ContainsAny(entry, "*?") {
			add(entry)
			continue
		}
		if files == nil {
			var err error
			if files, err = projectFiles(root); err != nil {
				return nil, err
			}
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

// ScriptToolingFiles are what a project's scripts folder holds for npm and the
// AssemblyScript compiler: never game assets.
var ScriptToolingFiles = []string{"package.json", "package-lock.json", "tsconfig.json", "asconfig.json"}

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
		if !strings.HasPrefix(name, ".") && !slices.Contains(ScriptToolingFiles, name) {
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
