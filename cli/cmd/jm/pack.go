package main

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
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

	// Pass 1: walk + collect .script.json's binary paths so we know which .wasm
	// files are bundled (vs. stray). fs.WalkDir does not follow symlinked
	// directories — intentional, archives should reflect committed sources only.
	consumedWasm := map[string]string{} // wasm-path → script.json-path for dup detection
	allFiles := []string{}
	err = fs.WalkDir(os.DirFS(buildDir), ".", func(path string, d fs.DirEntry, werr error) error {
		if werr != nil {
			return werr
		}
		if d.IsDir() {
			return nil
		}
		allFiles = append(allFiles, path)
		if strings.HasSuffix(path, ".script.json") {
			data, rerr := os.ReadFile(filepath.Join(buildDir, path))
			if rerr != nil {
				return fmt.Errorf("pack: read %s: %w", path, rerr)
			}
			sa, perr := manifest.LoadScriptAssetFromBytes(data)
			if perr != nil {
				return fmt.Errorf("pack: parse %s: %w", path, perr)
			}
			if sa.Binary == "" {
				return fmt.Errorf("pack: %s missing 'binary' field", path)
			}
			wasmKey := filepath.ToSlash(filepath.Clean(sa.Binary))
			if existing, dup := consumedWasm[wasmKey]; dup {
				return fmt.Errorf("pack: %s and %s both reference %s", existing, path, sa.Binary)
			}
			consumedWasm[wasmKey] = path
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
		e, err := classify(key, absPath, buildDir, consumedWasm, strict)
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
func classify(key, absPath, buildDir string, consumedWasm map[string]string, strict bool) (*archive.AssetEntry, error) {
	base := filepath.Base(key)
	ext := filepath.Ext(key)

	if strings.HasPrefix(base, ".") &&
		!strings.HasSuffix(key, archive.ManifestEntryKey) &&
		!strings.HasSuffix(key, ".script.json") &&
		!strings.HasSuffix(key, ".scene.json") &&
		!strings.HasSuffix(key, ".prefab.json") {
		return nil, nil
	}

	switch {
	case strings.HasSuffix(key, ".script.json"):
		return classifyScript(key, absPath, buildDir)
	case ext == ".wasm":
		if _, ok := consumedWasm[key]; !ok {
			return nil, fmt.Errorf("pack: stray %s (no .script.json references it)", key)
		}
		return nil, nil
	case strings.HasSuffix(key, ".scene.json"):
		return readEntry(key, absPath, "scene", nil)
	case strings.HasSuffix(key, ".prefab.json"):
		return readEntry(key, absPath, "prefab", nil)
	case strings.HasSuffix(key, archive.ManifestEntryKey):
		return readEntry(key, absPath, "manifest", nil)
	case ext == ".png" || ext == ".jpg" || ext == ".jpeg":
		return readEntry(key, absPath, "image", nil)
	case ext == ".wav" || ext == ".ogg":
		return readEntry(key, absPath, "audio", nil)
	default:
		if strict {
			return nil, fmt.Errorf("pack: unrecognized extension %q at %s", ext, key)
		}
		fmt.Fprintf(os.Stderr, "pack: skipping unrecognized %s\n", key)
		return nil, nil
	}
}

func classifyScript(key, absPath, buildDir string) (*archive.AssetEntry, error) {
	data, err := os.ReadFile(absPath)
	if err != nil {
		return nil, err
	}
	sa, err := manifest.LoadScriptAssetFromBytes(data)
	if err != nil {
		return nil, fmt.Errorf("pack: parse %s: %w", key, err)
	}
	if sa.Binary == "" {
		return nil, fmt.Errorf("pack: %s missing 'binary' field", key)
	}
	// `binary` is resolved relative to the build root, mirroring the engine's
	// runtime behavior (Engine.cpp loads via assetManager root, not relative
	// to the .script.json's directory).
	wasmAbs := filepath.Join(buildDir, filepath.FromSlash(sa.Binary))
	wasmBytes, err := os.ReadFile(wasmAbs)
	if err != nil {
		return nil, fmt.Errorf("pack: %s references missing %s: %w", key, sa.Binary, err)
	}
	return &archive.AssetEntry{
		SourcePath: key,
		Type:       "script",
		Metadata: map[string]interface{}{
			"name":    sa.Name,
			"imports": sa.Imports,
			"exposed": sa.Exposed,
		},
		Payload: wasmBytes,
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
