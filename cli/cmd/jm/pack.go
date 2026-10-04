package main

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
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

func runPack(buildDir, outFlag string, strict bool) error {
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

	// Pass 1: find which .atlas.png files atlases reference (the rest are
	// stray). WalkDir doesn't follow symlinks: archives hold committed files only.
	consumedAtlasImages := map[string]string{} // atlas.png-path → atlas.json-path for dup detection
	allFiles := []string{}
	err = fs.WalkDir(os.DirFS(buildDir), ".", func(path string, d fs.DirEntry, werr error) error {
		if werr != nil {
			return werr
		}
		if d.IsDir() {
			return nil
		}
		allFiles = append(allFiles, path)
		if strings.HasSuffix(path, ".atlas.json") {
			data, rerr := os.ReadFile(filepath.Join(buildDir, path))
			if rerr != nil {
				return fmt.Errorf("pack: read %s: %w", path, rerr)
			}
			var out atlas.AtlasOutput
			if perr := json.Unmarshal(data, &out); perr != nil {
				return fmt.Errorf("pack: parse atlas %s: %w", path, perr)
			}
			if out.Image == "" {
				return fmt.Errorf("pack: %s missing 'image' field", path)
			}
			if filepath.IsAbs(out.Image) || strings.Contains(out.Image, "..") {
				return fmt.Errorf("pack: %s image %q must be a build-root-relative path", path, out.Image)
			}
			imgKey := filepath.ToSlash(filepath.Clean(out.Image))
			if existing, dup := consumedAtlasImages[imgKey]; dup {
				return fmt.Errorf("pack: %s and %s both reference %s", existing, path, out.Image)
			}
			consumedAtlasImages[imgKey] = path
		}
		return nil
	})
	if err != nil {
		return err
	}

	// Pass 2: classify, build entries.
	sort.Strings(allFiles) // deterministic ordering
	entries := []archive.AssetEntry{}
	for _, path := range allFiles {
		key := filepath.ToSlash(filepath.Clean(path))
		absPath := filepath.Join(buildDir, path)
		e, err := classify(key, absPath, buildDir, consumedAtlasImages, strict)
		if err != nil {
			return err
		}
		if e == nil {
			continue
		}
		entries = append(entries, *e)
	}

	outPath := outFlag
	if outPath == "" {
		outPath = filepath.Join(buildDir, slugify(man.Name)+".jm")
	}

	f, err := os.Create(outPath)
	if err != nil {
		return fmt.Errorf("pack: create %s: %w", outPath, err)
	}
	defer f.Close()

	if err := archive.WriteArchive(f, entries); err != nil {
		return fmt.Errorf("pack: write: %w", err)
	}
	fmt.Printf("Packed %d entries → %s\n", len(entries), outPath)
	return nil
}

// classify returns nil for skipped files. Hidden files (other than known-good
// compound extensions) are silently skipped. Unrecognized extensions warn and
// skip in the default mode; --strict errors instead.
func classify(key, absPath, buildDir string, consumedAtlasImages map[string]string,
	strict bool) (*archive.AssetEntry, error) {
	base := filepath.Base(key)
	ext := filepath.Ext(key)

	if strings.HasPrefix(base, ".") &&
		!strings.HasSuffix(key, archive.ManifestEntryKey) &&
		!strings.HasSuffix(key, ".bindings.json") &&
		!strings.HasSuffix(key, ".scene.json") &&
		!strings.HasSuffix(key, ".prefab.json") &&
		!strings.HasSuffix(key, ".atlas.json") {
		return nil, nil
	}

	switch {
	case strings.HasSuffix(key, ".atlas.json"):
		return classifyAtlas(key, absPath, buildDir)
	case strings.HasSuffix(key, ".atlas.png"):
		if _, ok := consumedAtlasImages[key]; !ok {
			return nil, fmt.Errorf("pack: stray %s (no .atlas.json references it)", key)
		}
		return readEntry(key, absPath, "image", nil)
	case strings.HasSuffix(key, ".scene.json"):
		return readEntry(key, absPath, "scene", nil)
	case strings.HasSuffix(key, ".prefab.json"):
		return readEntry(key, absPath, "prefab", nil)
	case ext == ".ts":  // jm build leaves compiled wasm at each script's .ts path
		return readEntry(key, absPath, "script", nil)
	case strings.HasSuffix(key, archive.ManifestEntryKey):
		return readEntry(key, absPath, "manifest", nil)
	case ext == ".png" || ext == ".jpg" || ext == ".jpeg":
		return readEntry(key, absPath, "image", nil)
	case ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac":
		return readEntry(key, absPath, "audio", nil)
	case strings.HasSuffix(key, ".ui.html"):
		return readEntry(key, absPath, "ui", nil)
	case ext == ".css":
		return readEntry(key, absPath, "stylesheet", nil)
	case ext == ".frag":
		return readEntry(key, absPath, "shader", nil)
	case ext == ".ttf" || ext == ".otf":
		return readEntry(key, absPath, "font", nil)
	case strings.HasSuffix(key, ".bindings.json"):
		return readEntry(key, absPath, "bindings", nil)
	default:
		if strict {
			return nil, fmt.Errorf("pack: unrecognized extension %q at %s", ext, key)
		}
		fmt.Fprintf(os.Stderr, "pack: skipping unrecognized %s\n", key)
		return nil, nil
	}
}

func classifyAtlas(key, absPath, buildDir string) (*archive.AssetEntry, error) {
	data, err := os.ReadFile(absPath)
	if err != nil {
		return nil, err
	}
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
	if out.Regions == nil {
		// A user-authored .atlas.json that hasn't been built yet would have nil
		// Regions (the user only writes inputs; the baker fills regions). Pack
		// is supposed to see only the post-bake output shape, so nil here means
		// either build was skipped or someone hand-wrote a partial file. Reject
		// explicitly; an empty map is fine, nil is not.
		return nil, fmt.Errorf("pack: %s has nil regions (was the atlas built?)", key)
	}
	// Belt-and-suspenders: pass 1 already rejected abs/.. paths, but classify
	// is also called for tests that bypass the walk; cheap to re-check.
	if filepath.IsAbs(out.Image) || strings.Contains(out.Image, "..") {
		return nil, fmt.Errorf("pack: %s image %q must be a build-root-relative path", key, out.Image)
	}
	// Verify the referenced .atlas.png actually exists in build/. Without this,
	// packed archives can have atlas entries whose images are missing and the
	// engine falls back silently to the default texture.
	imageAbs := filepath.Join(buildDir, filepath.FromSlash(out.Image))
	if _, err := os.Stat(imageAbs); err != nil {
		return nil, fmt.Errorf("pack: %s references missing %s: %w", key, out.Image, err)
	}
	md := map[string]interface{}{
		"image":   out.Image,
		"width":   out.Width,
		"height":  out.Height,
		"filter":  out.Filter,
		"regions": out.Regions,
	}
	return &archive.AssetEntry{
		SourcePath: key,
		Type:       "atlas",
		Metadata:   md,
		Payload:    data,
	}, nil
}

func readEntry(key, absPath, assetType string, metadata map[string]interface{}) (*archive.AssetEntry, error) {
	data, err := os.ReadFile(absPath)
	if err != nil {
		return nil, err
	}
	return &archive.AssetEntry{
		SourcePath: key,
		Type:       assetType,
		Metadata:   metadata,
		Payload:    data,
	}, nil
}

var slugRe = regexp.MustCompile(`[^a-z0-9]+`)

func slugify(name string) string {
	s := strings.ToLower(strings.TrimSpace(name))
	s = slugRe.ReplaceAllString(s, "-")
	s = strings.Trim(s, "-")
	if s == "" {
		return "game"
	}
	return s
}
