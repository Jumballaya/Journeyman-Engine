package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io/fs"
	"maps"
	"os"
	"path"
	"path/filepath"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// A build's sources: what jm build reads, each file's time and size. A build
// records them; it's stale when they differ now (a file added, changed or
// gone), and jm run --watch rebuilds when they change.

// sourcesFile is where a build keeps its sources' fingerprint (hidden: never packed).
const sourcesFile = ".sources"

// buildSources are the game at root's sources: the manifest, its scenes, the
// files its assets match and everything in assets/ (atlas images, normal maps,
// the scripts' tooling files too), and its script libraries' files. Keys are
// game-relative, a library's absolute; a file named but missing is "".
func buildSources(root string) map[string]string {
	out := map[string]string{}
	stamp := func(key, file string) {
		out[key] = ""
		if info, err := os.Stat(file); err == nil && !info.IsDir() {
			out[key] = fmt.Sprint(info.ModTime().UnixNano(), " ", info.Size())
		}
	}
	stamp(archive.ManifestEntryKey, filepath.Join(root, archive.ManifestEntryKey))
	man, err := manifest.LoadManifest(filepath.Join(root, archive.ManifestEntryKey))
	if err != nil {
		return out
	}
	assets, _ := manifest.ExpandAssets(os.DirFS(root), append([]string{"assets/**"}, man.Assets...))
	for _, name := range manifest.ScriptToolingFiles {
		assets = append(assets, scriptsPkgDir+"/"+name)
	}
	for _, rel := range slices.Concat(assets, man.Scenes) {
		stamp(rel, filepath.Join(root, filepath.FromSlash(rel)))
		if ext := strings.ToLower(path.Ext(rel)); (ext == ".png" || ext == ".jpg" || ext == ".jpeg") && !atlas.IsNormalMap(rel) {
			n := atlas.NormalPath(rel) // as withNormalMaps finds it: one made later counts
			stamp(n, filepath.Join(root, filepath.FromSlash(n)))
		}
	}
	for _, dir := range man.ScriptLibraries {
		src := librarySource(root, dir)
		top, err := filepath.EvalSymlinks(src) // the folder it leads to
		if err != nil {
			out[src] = ""
			continue
		}
		_ = filepath.WalkDir(top, func(file string, d fs.DirEntry, err error) error {
			if err != nil {
				return nil
			}
			if d.IsDir() && file != top && (d.Name() == "node_modules" || strings.HasPrefix(d.Name(), ".")) {
				return filepath.SkipDir
			}
			if !d.IsDir() {
				stamp(file, file)
			}
			return nil
		})
	}
	return out
}

// sourcesPrint names sources as a whole, and this jm (its runtime goes in every build).
func sourcesPrint(sources map[string]string) string {
	h := sha256.New()
	fmt.Fprintf(h, "jm %s\n", version)
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
	recorded, err := os.ReadFile(filepath.Join(root, "build", sourcesFile))
	return err != nil || strings.TrimSpace(string(recorded)) != sourcesPrint(buildSources(root))
}
