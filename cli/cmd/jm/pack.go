package main

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

var packOutFlag string
var packStrictFlag bool

var packCmd = &cobra.Command{
	Use:   "pack [build-dir]",
	Short: "Pack a built game into a single .jm archive",
	Args:  cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		buildDir := "build"
		if len(args) == 1 {
			buildDir = args[0]
		}
		return runPack(buildDir, packOutFlag, packStrictFlag)
	},
}

func init() {
	packCmd.Flags().StringVar(&packOutFlag, "out", "", "Output archive path (default: build/<slug>.jm)")
	packCmd.Flags().BoolVar(&packStrictFlag, "strict", false, "Error on any unrecognized file")
}

func runPack(buildDir, outPath string, strict bool) error {
	info, err := os.Stat(buildDir)
	if err != nil {
		return fmt.Errorf("pack: build dir %q not found: %w", buildDir, err)
	}
	if !info.IsDir() {
		return fmt.Errorf("pack: %q is not a directory", buildDir)
	}
	manifestPath := filepath.Join(buildDir, archive.ManifestEntryKey)
	if _, err := os.Stat(manifestPath); err != nil {
		return fmt.Errorf("pack: missing %s — run `jm build` first", manifestPath)
	}
	man, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return fmt.Errorf("pack: parse manifest: %w", err)
	}

	// WalkDir doesn't follow symlinks: archives hold committed files only.
	var files []string
	err = fs.WalkDir(os.DirFS(buildDir), ".", func(path string, d fs.DirEntry, err error) error {
		if err == nil && !d.IsDir() {
			files = append(files, path)
		}
		return err
	})
	if err != nil {
		return err
	}
	sort.Strings(files) // deterministic ordering

	atlasImages := map[string]string{} // .atlas.png key → the .atlas.json using it
	var pngs []string
	var entries []archive.AssetEntry
	for _, key := range files {
		e, err := classify(key, filepath.Join(buildDir, key), buildDir, strict)
		if err != nil {
			return err
		}
		if e == nil {
			continue
		}
		if e.Type == "atlas" {
			img := filepath.ToSlash(filepath.Clean(e.Metadata["image"].(string)))
			if prior, dup := atlasImages[img]; dup {
				return fmt.Errorf("pack: %s and %s both reference %s", prior, key, img)
			}
			atlasImages[img] = key
		}
		if strings.HasSuffix(key, ".atlas.png") {
			pngs = append(pngs, key)
		}
		entries = append(entries, *e)
	}
	for _, png := range pngs {
		if _, ok := atlasImages[png]; !ok {
			return fmt.Errorf("pack: stray %s (no .atlas.json references it)", png)
		}
	}

	if outPath == "" {
		outPath = filepath.Join(buildDir, slugify(man.Name)+".jm")
	}
	// Written beside, then renamed: a failed pack leaves the old archive whole.
	partial := outPath + ".packing"
	f, err := os.Create(partial)
	if err != nil {
		return fmt.Errorf("pack: create %s: %w", partial, err)
	}
	err = archive.WriteArchive(f, entries)
	if closeErr := f.Close(); err == nil {
		err = closeErr
	}
	if err == nil {
		err = os.Rename(partial, outPath)
	}
	if err != nil {
		os.Remove(partial)
		return fmt.Errorf("pack: write %s: %w", outPath, err)
	}
	fmt.Printf("Packed %d entries → %s\n", len(entries), outPath)
	return nil
}

// Hidden files are skipped unless they end in one of these.
var hiddenKeepSuffixes = []string{archive.ManifestEntryKey, ".bindings.json", ".scene.json", ".prefab.json", ".atlas.json"}

// What every project's scripts folder holds for npm and the AssemblyScript compiler.
var scriptToolingFiles = []string{"package.json", "package-lock.json", "tsconfig.json", "asconfig.json"}

// assetTypes maps a file suffix to its archive entry type.
var assetTypes = []struct{ suffix, kind string }{
	{".atlas.json", "atlas"},
	{".scene.json", "scene"},
	{".prefab.json", "prefab"},
	{".bindings.json", "bindings"},
	{".tmj", "tilemap"}, {".tsj", "tileset"}, // Tiled JSON maps and tilesets
	{archive.ManifestEntryKey, "manifest"},
	{".ts", "script"}, // jm build leaves compiled wasm at each script's .ts path
	{".png", "image"}, {".jpg", "image"}, {".jpeg", "image"},
	{".wav", "audio"}, {".ogg", "audio"}, {".mp3", "audio"}, {".flac", "audio"},
	{".ui.html", "ui"},
	{".css", "stylesheet"},
	{".frag", "shader"},
	{".ttf", "font"}, {".otf", "font"},
}

// classify returns nil for skipped files: hidden ones, earlier archives, and
// (unless strict, which errors) unrecognized extensions, with a warning.
func classify(key, absPath, buildDir string, strict bool) (*archive.AssetEntry, error) {
	hasSuffix := func(s string) bool { return strings.HasSuffix(key, s) }
	if strings.HasPrefix(filepath.Base(key), ".") && !slices.ContainsFunc(hiddenKeepSuffixes, hasSuffix) {
		return nil, nil
	}
	if hasSuffix(".jm") {
		return nil, nil // an earlier pack's output
	}
	if filepath.ToSlash(filepath.Dir(key)) == scriptsPkgDir && slices.Contains(scriptToolingFiles, filepath.Base(key)) {
		return nil, nil // the compiler's, not the game's
	}
	i := slices.IndexFunc(assetTypes, func(t struct{ suffix, kind string }) bool { return hasSuffix(t.suffix) })
	if i < 0 {
		if strict {
			return nil, fmt.Errorf("pack: unrecognized extension %q at %s", filepath.Ext(key), key)
		}
		fmt.Fprintf(os.Stderr, "pack: skipping unrecognized %s\n", key)
		return nil, nil
	}
	data, err := os.ReadFile(absPath)
	if err != nil {
		return nil, err
	}
	e := &archive.AssetEntry{SourcePath: key, Type: assetTypes[i].kind, Payload: data}
	if e.Type == "atlas" {
		if e.Metadata, err = atlasMetadata(key, data, buildDir); err != nil {
			return nil, err
		}
	}
	return e, nil
}

// atlasMetadata validates a baked .atlas.json and returns its archive metadata.
func atlasMetadata(key string, data []byte, buildDir string) (map[string]any, error) {
	var out atlas.AtlasOutput
	if err := json.Unmarshal(data, &out); err != nil {
		return nil, fmt.Errorf("pack: parse atlas %s: %w", key, err)
	}
	if out.Image == "" {
		return nil, fmt.Errorf("pack: %s missing 'image' field", key)
	}
	if out.Width == 0 || out.Height == 0 {
		return nil, fmt.Errorf("pack: %s missing or zero width/height (got %dx%d)", key, out.Width, out.Height)
	}
	// The baker always writes regions (maybe empty); nil means an unbaked source.
	if out.Regions == nil {
		return nil, fmt.Errorf("pack: %s has nil regions (was the atlas built?)", key)
	}
	if validateRelativePath(out.Image) != nil {
		return nil, fmt.Errorf("pack: %s image %q must be a build-root-relative path", key, out.Image)
	}
	// Without its image the engine would silently fall back to the default texture.
	if _, err := os.Stat(filepath.Join(buildDir, filepath.FromSlash(out.Image))); err != nil {
		return nil, fmt.Errorf("pack: %s references missing %s: %w", key, out.Image, err)
	}
	return map[string]any{
		"image":   out.Image,
		"width":   out.Width,
		"height":  out.Height,
		"filter":  out.Filter,
		"regions": out.Regions,
	}, nil
}

var slugRe = regexp.MustCompile(`[^a-z0-9]+`)

func slugify(name string) string {
	s := strings.Trim(slugRe.ReplaceAllString(strings.ToLower(name), "-"), "-")
	if s == "" {
		return "game"
	}
	return s
}
