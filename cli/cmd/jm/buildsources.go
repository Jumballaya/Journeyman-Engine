package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io/fs"
	"maps"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"sync"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"
)

// A build's sources: every file it could read, each one's time and size. A
// build records them; it's stale when they differ now (a file added, changed
// or gone), and jm run --watch rebuilds when they change. Conservative: the
// whole game but what jm and runs write, since a build finds files as it goes
// (an atlas's images, a script's imports, the art note).

// sourcesFile is where a build keeps its sources' fingerprint (hidden: never packed).
const sourcesFile = ".sources"

// notSources are the game's own folders jm and its runs write: builds and
// their staging, exports, engine logs, captured frames. And node_modules anywhere: npm's.
var notSources = []string{"build", outDir, outDir + ".modules", "build.old", "dist", "logs", "frames"}

// buildSources are the game at root's sources: its files (not hidden ones, nor
// archives, nor in notSources), what its manifest names though missing (""),
// and its script libraries' files, as jm build copies them.
func buildSources(root string) map[string]string {
	out := map[string]string{}
	stamp := func(key, file string) {
		out[key] = ""
		if info, err := os.Stat(file); err == nil && !info.IsDir() {
			out[key] = fmt.Sprint(info.ModTime().UnixNano(), " ", info.Size())
		}
	}
	realRoot := evalOrSelf(root)
	// walk stamps top's files; game: the game's folder (hidden ones, archives and notSources aren't its sources).
	walk := func(top string, game bool, key func(string) string) {
		_ = filepath.WalkDir(top, func(file string, d fs.DirEntry, err error) error {
			if err != nil || file == top {
				return nil
			}
			name := d.Name()
			mine := game && (strings.HasPrefix(name, ".") || (filepath.Dir(file) == top && slices.Contains(notSources, name)))
			if d.IsDir() {
				if mine || name == "node_modules" || file == realRoot {
					return filepath.SkipDir // file == realRoot: a library holding the game, whose files are the game's
				}
				return nil
			}
			if !mine && !(game && strings.HasSuffix(name, ".jm")) {
				stamp(key(file), file)
			}
			return nil
		})
	}
	walk(realRoot, true, func(file string) string { rel, _ := filepath.Rel(realRoot, file); return filepath.ToSlash(rel) })
	stamp(archive.ManifestEntryKey, filepath.Join(root, archive.ManifestEntryKey))
	man, err := manifest.LoadManifest(filepath.Join(root, archive.ManifestEntryKey))
	if err != nil {
		return out
	}
	for _, rel := range slices.Concat(man.Scenes, man.Assets) {
		if _, seen := out[rel]; !seen && !strings.ContainsAny(rel, "*?") {
			stamp(rel, filepath.Join(root, filepath.FromSlash(rel)))
		}
	}
	for _, dir := range man.ScriptLibraries {
		src := librarySource(root, dir)
		top, err := filepath.EvalSymlinks(src) // the folder it leads to
		if err != nil {
			out[src] = ""
			continue
		}
		if top == realRoot || strings.HasPrefix(top, realRoot+string(filepath.Separator)) {
			continue // in the game: walked as the game's
		}
		out[src] = "library " + top // where it leads: pointed elsewhere is a change
		walk(top, false, func(file string) string { return file })
	}
	return out
}

// runtimePrint names the @jm/runtime and @jm/behaviors this jm puts in every
// build: a dev jm's version doesn't change when they do.
var runtimePrint = sync.OnceValue(func() string {
	h := sha256.New()
	_ = fs.WalkDir(stdlib.StdLibFiles, ".", func(path string, d fs.DirEntry, err error) error {
		if err == nil && !d.IsDir() {
			data, _ := fs.ReadFile(stdlib.StdLibFiles, path)
			fmt.Fprintf(h, "%s\x00%d\x00", path, len(data))
			h.Write(data)
		}
		return nil
	})
	return hex.EncodeToString(h.Sum(nil))
})

// sourcesPrint names sources as a whole, with this jm's runtime.
func sourcesPrint(sources map[string]string) string {
	h := sha256.New()
	fmt.Fprintf(h, "jm %s %s\n", version, runtimePrint())
	for _, key := range slices.Sorted(maps.Keys(sources)) {
		fmt.Fprintf(h, "%s\x00%s\n", key, sources[key])
	}
	return hex.EncodeToString(h.Sum(nil))
}

// recordSources keeps sources with the build in dir, as made from them.
func recordSources(dir string, sources map[string]string) error {
	return os.WriteFile(filepath.Join(dir, sourcesFile), []byte(sourcesPrint(sources)+"\n"), 0o644)
}

// buildIsStale says whether the game at root has no build, or one made from
// other sources than it has now.
func buildIsStale(root string) bool {
	if !exists(filepath.Join(root, "build", archive.ManifestEntryKey)) {
		return true
	}
	recorded, err := os.ReadFile(filepath.Join(root, "build", sourcesFile))
	return err != nil || strings.TrimSpace(string(recorded)) != sourcesPrint(buildSources(root))
}
