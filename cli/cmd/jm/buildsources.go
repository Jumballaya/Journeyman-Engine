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
	"github.com/Jumballaya/Journeyman-Engine/internal/gametree"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/realpath"
	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"
)

// A build's sources: every file it could read, each one's time and size. A
// build records them; it's stale when they differ now (a file added, changed
// or gone), and jm run --watch rebuilds when they change. Conservative: the
// whole game but what jm and runs write, since a build finds files as it goes
// (an atlas's images, a script's imports, the art note).

// sourcesFile is where a build keeps its sources' fingerprint (hidden: never packed).
const sourcesFile = ".sources"

// runOutputs are the JM_* variables naming where a run writes (as peerEnv
// gives each peer its own): set in this process (jm run --watch), not sources.
var runOutputs = []string{"JM_CAPTURE_DIR", "JM_DUMP_DIR", "JM_SAVE_DIR", "JM_RECORD_DIR", "JM_NET_TRACE", "JM_DRIVE_RECORD", "JM_ERRORS", "JM_PROFILE",
	"JM_CONTROL", "JM_EDITOR_CONTROL", "JM_EDITOR_CAPTURE"}

// buildSources are the game at root's sources: its files but hidden ones,
// archives and what jm and runs write; what its manifest names though missing
// (""); and its script libraries' files as jm build copies them (all but node_modules).
func buildSources(root string) map[string]string {
	out := map[string]string{}
	stamp := func(key, file string) {
		out[key] = ""
		if info, err := os.Stat(file); err == nil && !info.IsDir() {
			out[key] = fmt.Sprint(info.ModTime().UnixNano(), " ", info.Size())
		}
	}
	realRoot := evalOrSelf(root)
	var outputs []string
	for _, v := range runOutputs {
		p := os.Getenv(v)
		if p == "" || p == "-" {
			continue
		}
		if !filepath.IsAbs(p) {
			p = filepath.Join(realRoot, p)
		}
		p = filepath.Join(evalOrSelf(filepath.Dir(p)), filepath.Base(p)) // named as the walk names files (it may not be there yet)
		if realpath.In(p, realRoot) {
			continue // the game, or a folder holding it: its outputs can't be told from its sources
		}
		outputs = append(outputs, p)
	}
	// output: file is one jm or a run writes.
	output := func(file string) bool {
		return slices.ContainsFunc(outputs, func(o string) bool { return file == o || strings.HasPrefix(file, o+string(filepath.Separator)) })
	}
	// walk stamps top's files; the game's (top is the game) but what isn't the
	// game's (gametree), hidden files and archives; a library's but its npm installs.
	walk := func(top string, key func(string) string) {
		game := top == realRoot
		_ = filepath.WalkDir(top, func(file string, d fs.DirEntry, err error) error {
			if err != nil || file == top {
				return nil
			}
			name := d.Name()
			skip := output(file)
			if d.IsDir() {
				rel, _ := filepath.Rel(realRoot, file)
				skip = skip || game && gametree.Skip(filepath.ToSlash(rel)) || name == "node_modules"

				if skip || (!game && file == realRoot) { // a library holding the game: the game's files are the game's
					return filepath.SkipDir
				}
				return nil
			}
			if !skip && !(game && (strings.HasPrefix(name, ".") || strings.HasSuffix(name, ".jm"))) {
				stamp(key(file), file)
			}
			return nil
		})
	}
	walk(realRoot, func(file string) string { rel, _ := filepath.Rel(realRoot, file); return filepath.ToSlash(rel) })
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
		out[src] = "library " + top // where it leads: pointed elsewhere is a change
		if top != realRoot {
			walk(top, func(file string) string { return file })
		}
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
