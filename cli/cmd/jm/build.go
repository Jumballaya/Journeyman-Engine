package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"image"
	"image/png"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"
	"github.com/Jumballaya/Journeyman-Engine/internal/wasm"

	"github.com/spf13/cobra"
)

// Minimum Node.js major version supported. Pinned to match assemblyscript's
// engines.node requirement (AS 0.28+ requires Node ≥ 20).
const minNodeMajor = 20

// Path the npm project lives at, relative to the project root. Detection and
// asc invocation both anchor on this path.
const scriptsPkgDir = "assets/scripts"

// collectedScriptMetadata accumulates per-script metadata (imports, exposed)
// produced by the AssemblyScript compile path. Pack consumes this to populate
// resolver-entry metadata in archive mode. Reset at the start of each build
// invocation — necessary for tests that drive Run repeatedly in one process.
var collectedScriptMetadata = map[string]stdlib.ScriptMetaData{}

func recordScriptMetadata(tsPath string, md stdlib.ScriptMetaData) {
	collectedScriptMetadata[tsPath] = md
}

func resetScriptMetadata() {
	collectedScriptMetadata = map[string]stdlib.ScriptMetaData{}
}

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build game assets and compile AssemblyScript",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("Building game...")

		resetScriptMetadata()

		projectRoot, err := os.Getwd()
		exitOnError("Failed to resolve project root", err)

		// Five fail-fast checks before any work, so the user gets a
		// targeted message instead of a cryptic mid-build failure. The
		// returned errors are already user-actionable, so print directly
		// rather than letting exitOnError prepend a redundant prefix.
		if err := checkBuildPrereqs(projectRoot); err != nil {
			fmt.Println(err)
			os.Exit(1)
		}

		// npm install prunes packages not listed in package.json — that
		// includes @jm/runtime, which we ship via go:embed rather than npm.
		// Re-extract it before invoking asc so the resolver finds it.
		exitOnError("Failed to sync @jm/runtime", syncEmbeddedRuntime(projectRoot))

		manifestData, err := manifest.LoadManifest(archive.ManifestEntryKey)
		exitOnError("Error loading manifest", err)

		// Validate every manifest-listed path BEFORE any filesystem mutation.
		// Otherwise a malicious or buggy manifest could trigger MkdirAll +
		// copy attempts for `../../etc/foo` before path-traversal checks run.
		for _, p := range manifestData.Assets {
			if err := validateRelativePath(p); err != nil {
				exitOnError(fmt.Sprintf("Invalid manifest asset path %q", p), err)
			}
		}
		for _, p := range manifestData.Scenes {
			if err := validateRelativePath(p); err != nil {
				exitOnError(fmt.Sprintf("Invalid manifest scene path %q", p), err)
			}
		}

		// Pre-migration: scenes reference legacy `.script.json` paths but the
		// runtime now resolves scripts at `.ts` paths. Build a rewrite map from
		// `.script.json` entries in `assets[]` so processScenes can substitute
		// `.ts` references on the fly. Post-migration the map is empty and the
		// rewrite is a no-op.
		scriptJsonToTs := buildLegacyScriptMap(manifestData.Assets)

		copyFileOrExit(archive.ManifestEntryKey, filepath.Join("build", archive.ManifestEntryKey))

		processAssets(manifestData.Assets, projectRoot)
		processAtlases(manifestData.Assets)
		processScenes(manifestData.Scenes, scriptJsonToTs)

		fmt.Println("Build complete!")
	},
}

// checkBuildPrereqs verifies the toolchain and per-project npm install state
// before any compile work happens. Returns a wrapped error with an actionable
// message; nil if all checks pass. The five distinct cases below have distinct
// fix instructions, so we report them separately rather than collapsing to a
// generic "something's missing".
func checkBuildPrereqs(projectRoot string) error {
	if _, err := exec.LookPath("node"); err != nil {
		return fmt.Errorf("Node.js not found in PATH. Install Node ≥ %d (https://nodejs.org/) then re-run", minNodeMajor)
	}

	major, raw, err := nodeMajorVersion()
	if err != nil {
		return fmt.Errorf("could not determine Node version: %w", err)
	}
	if major < minNodeMajor {
		return fmt.Errorf("Node.js ≥ %d required, found %s. Upgrade Node and re-run", minNodeMajor, raw)
	}

	scriptsDir := filepath.Join(projectRoot, scriptsPkgDir)
	nodeModules := filepath.Join(scriptsDir, "node_modules")
	if _, err := os.Stat(nodeModules); err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("npm dependencies not installed. Run: cd %s && npm install", scriptsPkgDir)
		}
		return fmt.Errorf("stat %s: %w", nodeModules, err)
	}

	asPkg := filepath.Join(nodeModules, "assemblyscript")
	if _, err := os.Stat(asPkg); err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("AssemblyScript missing from %s/node_modules. Run: cd %s && npm install", scriptsPkgDir, scriptsPkgDir)
		}
		return fmt.Errorf("stat %s: %w", asPkg, err)
	}

	return nil
}

// syncEmbeddedRuntime materializes the @jm/runtime package into the project's
// node_modules. The runtime ships embedded in the jm binary; npm install would
// prune it (it's not in package.json). Wipes the destination first so files
// removed in a future jm release don't survive into stale local copies.
func syncEmbeddedRuntime(projectRoot string) error {
	dst := filepath.Join(projectRoot, scriptsPkgDir, "node_modules", "@jm", "runtime")
	if err := os.RemoveAll(dst); err != nil {
		return fmt.Errorf("clear %s: %w", dst, err)
	}
	if err := os.MkdirAll(dst, 0755); err != nil {
		return fmt.Errorf("mkdir %s: %w", dst, err)
	}
	return fs.WalkDir(stdlib.StdLibFiles, "runtime", func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		// Strip the `runtime/` prefix — files land flat in dst.
		rel := strings.TrimPrefix(path, "runtime/")
		out := filepath.Join(dst, rel)
		data, err := stdlib.StdLibFiles.ReadFile(path)
		if err != nil {
			return fmt.Errorf("read embedded %s: %w", path, err)
		}
		if err := os.MkdirAll(filepath.Dir(out), 0755); err != nil {
			return fmt.Errorf("mkdir %s: %w", filepath.Dir(out), err)
		}
		if err := os.WriteFile(out, data, 0644); err != nil {
			return fmt.Errorf("write %s: %w", out, err)
		}
		return nil
	})
}

// nodeMajorVersion returns the major version reported by `node --version`.
// Output looks like "v20.10.0\n" or "v18.17.1-pre+build" for pre-releases —
// strip the leading "v", split on the first non-digit, parse what's before.
// Wraps the exec in a 5-second timeout so a hung shim on PATH can't stall
// the build forever.
func nodeMajorVersion() (int, string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	out, err := exec.CommandContext(ctx, "node", "--version").Output()
	if err != nil {
		return 0, "", err
	}
	raw := strings.TrimSpace(string(out))
	v := strings.TrimPrefix(raw, "v")
	end := 0
	for end < len(v) && v[end] >= '0' && v[end] <= '9' {
		end++
	}
	if end == 0 {
		return 0, raw, fmt.Errorf("unparseable node version: %q", raw)
	}
	major, err := strconv.Atoi(v[:end])
	if err != nil {
		return 0, raw, fmt.Errorf("parse node major from %q: %w", raw, err)
	}
	return major, raw, nil
}

// buildLegacyScriptMap reads each `.script.json` entry in `assets[]` and maps
// it to the `.ts` path it references. Used pre-migration to rewrite scenes
// that still point at `.script.json` files. Errors during read are logged but
// don't abort the build — the entry is just absent from the rewrite map, which
// surfaces later as a clearer "no rewrite for X" message during scene walking.
func buildLegacyScriptMap(assets []string) map[string]string {
	m := map[string]string{}
	for _, a := range assets {
		if !strings.HasSuffix(a, ".script.json") {
			continue
		}
		data, err := os.ReadFile(a)
		if err != nil {
			fmt.Fprintf(os.Stderr, "warning: skipping %s in scene-rewrite map: %v\n", a, err)
			continue
		}
		sa, err := manifest.LoadScriptAssetFromBytes(data)
		if err != nil {
			fmt.Fprintf(os.Stderr, "warning: skipping %s in scene-rewrite map: %v\n", a, err)
			continue
		}
		key := filepath.ToSlash(filepath.Clean(a))
		val := filepath.ToSlash(filepath.Clean(sa.Script))
		m[key] = val
	}
	return m
}

func processAssets(assets []string, projectRoot string) {
	for _, asset := range assets {
		dst := filepath.Join("build", asset)
		copyFileOrExit(asset, dst)
		fmt.Printf("Copied asset: %s\n", asset)

		switch {
		case strings.HasSuffix(asset, ".ts"):
			buildScriptFromTs(asset, projectRoot)
		case strings.HasSuffix(asset, ".script.json"):
			buildScript(asset, projectRoot)
		}
	}
}

// processAtlases re-walks manifest assets[] for .atlas.json configs, packs
// their listed source PNGs into an atlas image, and emits BOTH the packed
// .atlas.png and an augmented .atlas.json (with computed regions + dims) at
// build/<config-path-mirror>.
//
// processAssets has already copied each .atlas.json to build/ verbatim; this
// phase OVERWRITES that copy with the augmented form. Source PNGs are NOT
// listed in manifest.Assets per F's authoring rule — they're inputs, not
// shipped assets — so this phase reads them directly from the project's
// source tree (jm build's cwd is the project root).
func processAtlases(assets []string) {
	for _, a := range assets {
		if !strings.HasSuffix(a, ".atlas.json") {
			continue
		}
		data, err := os.ReadFile(a)
		exitOnError(fmt.Sprintf("atlas: read %s", a), err)

		cfg, err := atlas.LoadConfig(data)
		exitOnError(fmt.Sprintf("atlas: parse %s", a), err)

		if len(cfg.Sources) == 0 {
			exitOnError(
				fmt.Sprintf("atlas: %s: missing or empty 'sources'", a),
				fmt.Errorf("config has no sources"),
			)
		}

		// Friendlier-than-Pack's-backstop error if two source basenames
		// would collide as region names.
		err = checkUniqueBasenames(cfg.Sources)
		exitOnError(fmt.Sprintf("atlas: %s", a), err)

		srcImgs, err := loadAtlasSources(cfg.Sources)
		exitOnError(fmt.Sprintf("atlas: %s: load sources", a), err)

		atlasImg, regions, err := atlas.Pack(srcImgs, cfg.Padding, atlasMaxOrDefault(cfg))
		exitOnError(fmt.Sprintf("atlas: %s: pack", a), err)

		// Output paths mirror the source-tree path under build/.
		atlasJsonOut := filepath.Join("build", a)
		atlasPngOut := strings.TrimSuffix(atlasJsonOut, ".atlas.json") + ".atlas.png"
		err = os.MkdirAll(filepath.Dir(atlasPngOut), 0o755)
		exitOnError(fmt.Sprintf("atlas: %s: mkdir", a), err)

		err = writeAtlasPng(atlasPngOut, atlasImg)
		exitOnError(fmt.Sprintf("atlas: %s: write png", a), err)

		// `image` field references the sibling .atlas.png by build-root-
		// relative path (the same path the resolver will key on at pack
		// time). filepath.ToSlash ensures forward slashes regardless of
		// host OS — matches Archive's path canonicalization invariant.
		imageRel := strings.TrimSuffix(a, ".atlas.json") + ".atlas.png"
		out := atlas.AtlasOutput{
			Image:   filepath.ToSlash(filepath.Clean(imageRel)),
			Width:   atlasImg.Bounds().Dx(),
			Height:  atlasImg.Bounds().Dy(),
			Filter:  filterOrDefault(cfg.Filter),
			Regions: regions,
		}
		outBytes, err := json.MarshalIndent(out, "", "  ")
		exitOnError(fmt.Sprintf("atlas: %s: marshal output", a), err)

		err = os.WriteFile(atlasJsonOut, outBytes, 0o644)
		exitOnError(fmt.Sprintf("atlas: %s: write augmented json", a), err)

		fmt.Printf("Atlas: %s (%dx%d, %d regions)\n",
			a, out.Width, out.Height, len(out.Regions))
	}
}

// loadAtlasSources reads each PNG from the source tree, decodes it, and
// returns SourceImages keyed by basename-minus-extension. Rejects absolute
// paths and any path containing `..` segments — atlas sources must be
// project-root-relative for reproducibility.
func loadAtlasSources(paths []string) ([]atlas.SourceImage, error) {
	out := make([]atlas.SourceImage, 0, len(paths))
	for _, p := range paths {
		if filepath.IsAbs(p) {
			return nil, fmt.Errorf("atlas source must be relative: %q", p)
		}
		clean := filepath.Clean(p)
		if strings.HasPrefix(clean, "..") {
			return nil, fmt.Errorf("atlas source escapes project root: %q", p)
		}
		f, err := os.Open(p)
		if err != nil {
			return nil, fmt.Errorf("open %q: %w", p, err)
		}
		img, err := png.Decode(f)
		closeErr := f.Close()
		if err != nil {
			return nil, fmt.Errorf("decode %q: %w", p, err)
		}
		if closeErr != nil {
			return nil, fmt.Errorf("close %q: %w", p, closeErr)
		}
		base := filepath.Base(p)
		name := strings.TrimSuffix(base, filepath.Ext(base))
		out = append(out, atlas.SourceImage{Name: name, Img: img})
	}
	return out, nil
}

// checkUniqueBasenames detects two sources whose basename-minus-extension
// collide. Region names are scoped to a single atlas (cross-atlas collisions
// are fine), so this only checks within `paths`.
//
// Errors with both colliding source paths in the message — friendlier than
// Pack's defensive backstop, which sees only the derived Name.
func checkUniqueBasenames(paths []string) error {
	seen := map[string]string{} // basename → first path that used it
	for _, p := range paths {
		base := filepath.Base(p)
		name := strings.TrimSuffix(base, filepath.Ext(base))
		if prior, dup := seen[name]; dup {
			return fmt.Errorf(
				"sources %q and %q both produce region name %q",
				prior, p, name)
		}
		seen[name] = p
	}
	return nil
}

// writeAtlasPng encodes via stdlib png.Encoder and writes via os.WriteFile.
// Encoder is deterministic within a pinned Go version (cross-version flate
// tuning may differ — CI pins go.mod's go directive, so this is fine).
func writeAtlasPng(path string, img *image.NRGBA) error {
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		return fmt.Errorf("encode %s: %w", path, err)
	}
	if err := os.WriteFile(path, buf.Bytes(), 0o644); err != nil {
		return fmt.Errorf("write %s: %w", path, err)
	}
	return nil
}

// atlasMaxOrDefault returns cfg.MaxSize if non-zero, else 4096 (the OpenGL
// 4.1 GL_MAX_TEXTURE_SIZE floor — see AtlasConfig docs for rationale).
func atlasMaxOrDefault(cfg atlas.AtlasConfig) int {
	if cfg.MaxSize <= 0 {
		return 4096
	}
	return cfg.MaxSize
}

// filterOrDefault normalizes the filter field. Unrecognized values fall
// back to "nearest" (the default for pixel-art atlases). Schema-only for
// F.1–F.6 — engine reads but doesn't apply (locked, see plan F.next.B).
func filterOrDefault(s string) string {
	switch s {
	case "nearest", "linear":
		return s
	default:
		return "nearest"
	}
}

// buildScript handles the legacy `.script.json` build path. Loads the manifest,
// invokes asc on the referenced `.ts` source, parses imports/exports from the
// resulting wasm, writes augmented `.script.json` to the build tree, and
// mirrors the wasm bytes at the `.ts` build path so the engine's `.ts`
// converter and `.script.json` converter both resolve to the same artifact.
func buildScript(assetPath, projectRoot string) {
	scriptAsset, err := manifest.LoadScriptAsset(assetPath)
	exitOnError(fmt.Sprintf("Failed to load script asset %s", assetPath), err)

	if err := validateRelativePath(scriptAsset.Binary); err != nil {
		exitOnError(fmt.Sprintf("Invalid binary path in %s", assetPath), err)
	}
	if err := validateRelativePath(scriptAsset.Script); err != nil {
		exitOnError(fmt.Sprintf("Invalid script path in %s", assetPath), err)
	}

	outputWasm := filepath.Join("build", scriptAsset.Binary)
	fmt.Printf("Building script: %s → %s\n", scriptAsset.Script, scriptAsset.Binary)

	if err := runAsc(scriptAsset.Script, outputWasm, projectRoot); err != nil {
		exitOnError(fmt.Sprintf("Build failed for script %s", scriptAsset.Script), err)
	}

	// Read the built wasm once and reuse the bytes for both metadata extraction
	// and the `.ts`-path mirror below.
	wasmBytes, err := os.ReadFile(outputWasm)
	exitOnError(fmt.Sprintf("Failed to read built wasm %s", outputWasm), err)
	metadata, err := parseWasmMetadata(wasmBytes)
	exitOnError(fmt.Sprintf("Metadata extraction failed for %s", scriptAsset.Script), err)

	scriptAsset.Imports = metadata.Imports
	scriptAsset.Exposed = metadata.Exposed

	assetData, err := json.MarshalIndent(scriptAsset, "", "	")
	if err != nil {
		exitOnError("Failed to serialize script asset to JSON", err)
	}
	outPath := filepath.Join("build", assetPath)
	if err := os.WriteFile(outPath, assetData, 0644); err != nil {
		exitOnError(fmt.Sprintf("Failed to write script asset to %s", outPath), err)
	}

	// Mirror the wasm bytes at the `.ts` build path so the post-migrate scene
	// rewrite (which substitutes `.script.json` refs with their `.ts` source)
	// resolves against the same root regardless of which extension a scene
	// references. The legacy converter (`.script.json`) and the new converter
	// (`.ts`) both see the same wasm; either entry point loads the script
	// identically.
	tsArtifact := filepath.Join("build", filepath.FromSlash(scriptAsset.Script))
	if err := os.MkdirAll(filepath.Dir(tsArtifact), os.ModePerm); err != nil {
		exitOnError(fmt.Sprintf("Failed to create build dir %s", filepath.Dir(tsArtifact)), err)
	}
	if err := os.WriteFile(tsArtifact, wasmBytes, 0644); err != nil {
		exitOnError(fmt.Sprintf("Failed to write wasm at .ts artifact path %s", tsArtifact), err)
	}
	recordScriptMetadata(filepath.ToSlash(filepath.Clean(scriptAsset.Script)), metadata)

	fmt.Printf("Built script: %s → %s\n", scriptAsset.Script, outputWasm)
}

// buildScriptFromTs handles the new `.ts` build path. The `.ts` is the source
// of truth; the build artifact lands at the same relative path inside `build/`
// (no collision — different roots). The bytes ARE wasm; they overwrite the
// TypeScript that copyFileOrExit placed there moments ago.
func buildScriptFromTs(tsPath, projectRoot string) {
	if err := validateRelativePath(tsPath); err != nil {
		exitOnError(fmt.Sprintf("Invalid script path %s", tsPath), err)
	}

	outputWasm := filepath.Join("build", tsPath)
	if err := runAsc(tsPath, outputWasm, projectRoot); err != nil {
		exitOnError(fmt.Sprintf("asc failed for %s", tsPath), err)
	}

	metadata, err := extractWasmMetadata(outputWasm)
	exitOnError(fmt.Sprintf("metadata extraction failed for %s", tsPath), err)
	recordScriptMetadata(filepath.ToSlash(filepath.Clean(tsPath)), metadata)

	fmt.Printf("Built script: %s → %s (imports: %v)\n", tsPath, outputWasm, metadata.Imports)
}

// runAsc invokes `npx asc` with CWD set to the scripts package directory, so
// asc can resolve `@jm/runtime` via standard Node module resolution from
// `assets/scripts/node_modules/`. Both `scriptPath` and `outputPath` are
// manifest-root-relative; the function translates the script path to be
// scripts-dir-relative and computes an absolute output path so the wasm lands
// in the build tree regardless of asc's CWD.
func runAsc(scriptPath, outputPath, projectRoot string) error {
	scriptsDir := filepath.Join(projectRoot, scriptsPkgDir)
	scriptAbs := filepath.Join(projectRoot, scriptPath)
	relScript, err := filepath.Rel(scriptsDir, scriptAbs)
	if err != nil {
		return fmt.Errorf("compute relative script path: %w", err)
	}

	outputAbs := filepath.Join(projectRoot, outputPath)
	if err := os.MkdirAll(filepath.Dir(outputAbs), 0755); err != nil {
		return fmt.Errorf("mkdir %s: %w", filepath.Dir(outputAbs), err)
	}

	cmd := exec.Command("npx", "asc",
		relScript,
		"--config", "asconfig.json",
		"--outFile", outputAbs,
	)
	cmd.Dir = scriptsDir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// parseWasmMetadata pulls function imports and exports from already-loaded
// wasm bytes. Both fields are inspection metadata for archive mode; the engine
// ignores them at runtime. Callers that don't need the bytes for anything else
// can use extractWasmMetadata which reads from a path.
func parseWasmMetadata(wasmBytes []byte) (stdlib.ScriptMetaData, error) {
	imports, exports, err := wasm.ParseImportsAndExports(wasmBytes)
	if err != nil {
		return stdlib.ScriptMetaData{}, fmt.Errorf("parse wasm: %w", err)
	}
	return stdlib.ScriptMetaData{Imports: imports, Exposed: exports}, nil
}

// extractWasmMetadata reads a wasm file and pulls out function imports and
// exports.
func extractWasmMetadata(wasmPath string) (stdlib.ScriptMetaData, error) {
	bytes, err := os.ReadFile(wasmPath)
	if err != nil {
		return stdlib.ScriptMetaData{}, fmt.Errorf("read wasm %s: %w", wasmPath, err)
	}
	md, err := parseWasmMetadata(bytes)
	if err != nil {
		return stdlib.ScriptMetaData{}, fmt.Errorf("%s: %w", wasmPath, err)
	}
	return md, nil
}

// validateRelativePath rejects absolute paths, paths with a Windows drive or
// UNC volume, paths with any `..` segment, and the empty string. This blocks
// a malicious or buggy manifest from writing outside the build tree via
// `"binary": "../../etc/foo.wasm"`, `"\\\\server\\share\\foo"`, or similar.
//
// Implementation note: we split-and-compare segments rather than using
// `strings.Contains(cleaned, "..")` because that substring match also matches
// legitimate names like `..foo` or `foo..bar`.
func validateRelativePath(p string) error {
	if p == "" {
		return fmt.Errorf("empty path not allowed")
	}
	if filepath.IsAbs(p) {
		return fmt.Errorf("absolute path not allowed: %s", p)
	}
	if filepath.VolumeName(p) != "" {
		return fmt.Errorf("volume-rooted path not allowed: %s", p)
	}
	cleaned := filepath.ToSlash(filepath.Clean(p))
	for _, seg := range strings.Split(cleaned, "/") {
		if seg == ".." {
			return fmt.Errorf("path traversal not allowed: %s", p)
		}
	}
	return nil
}

func processScenes(scenes []string, scriptJsonToTs map[string]string) {
	for _, scene := range scenes {
		rewriteSceneFile(scene, scriptJsonToTs)
	}
}

func rewriteSceneFile(scenePath string, scriptJsonToTs map[string]string) {
	data, err := os.ReadFile(scenePath)
	exitOnError(fmt.Sprintf("read scene %s", scenePath), err)

	if len(scriptJsonToTs) == 0 {
		// No rewrites to perform — preserve byte-exact source so re-running
		// build is a stable no-op on the scene file.
		dst := filepath.Join("build", scenePath)
		exitOnError(fmt.Sprintf("create build dir for scene %s", scenePath),
			os.MkdirAll(filepath.Dir(dst), os.ModePerm))
		exitOnError(fmt.Sprintf("write scene %s", dst),
			os.WriteFile(dst, data, 0644))
		fmt.Printf("Copied scene: %s\n", scenePath)
		return
	}

	var sceneJson map[string]interface{}
	if err := json.Unmarshal(data, &sceneJson); err != nil {
		exitOnError(fmt.Sprintf("parse scene %s", scenePath), err)
	}
	walkSceneAndRewrite(sceneJson, scriptJsonToTs, scenePath)
	out, err := json.MarshalIndent(sceneJson, "", "  ")
	exitOnError(fmt.Sprintf("marshal scene %s", scenePath), err)

	dst := filepath.Join("build", scenePath)
	exitOnError(fmt.Sprintf("create build dir for scene %s", scenePath),
		os.MkdirAll(filepath.Dir(dst), os.ModePerm))
	exitOnError(fmt.Sprintf("write scene %s", dst),
		os.WriteFile(dst, out, 0644))
	fmt.Printf("Rewrote scene: %s\n", scenePath)
}

// walkSceneAndRewrite recursively descends `entities[].components.ScriptComponent.script`.
// In-place rewrites if the value is in the map. Logs (does not abort) for refs
// that aren't in the map — the user re-introduced a stale reference; engine
// will fail at scene load with "asset not found." Acceptable.
//
// @TODO: extend if more components reference scripts. Today's only script
// reference site is ScriptComponent.script.
func walkSceneAndRewrite(node interface{}, scriptJsonToTs map[string]string, scenePath string) {
	switch v := node.(type) {
	case map[string]interface{}:
		if comps, ok := v["components"].(map[string]interface{}); ok {
			if sc, ok := comps["ScriptComponent"].(map[string]interface{}); ok {
				if ref, ok := sc["script"].(string); ok && strings.HasSuffix(ref, ".script.json") {
					key := filepath.ToSlash(filepath.Clean(ref))
					if newRef, found := scriptJsonToTs[key]; found {
						sc["script"] = newRef
					} else {
						fmt.Printf("scene %s: no rewrite for %q (not in assets[])\n", scenePath, ref)
					}
				}
			}
		}
		for _, child := range v {
			walkSceneAndRewrite(child, scriptJsonToTs, scenePath)
		}
	case []interface{}:
		for _, child := range v {
			walkSceneAndRewrite(child, scriptJsonToTs, scenePath)
		}
	}
}

func copyFileOrExit(src, dst string) {
	exitOnError(fmt.Sprintf("Failed to create dir %s", filepath.Dir(dst)),
		os.MkdirAll(filepath.Dir(dst), os.ModePerm))
	exitOnError(fmt.Sprintf("Failed to copy %s → %s", src, dst), copyFile(src, dst))
}

func copyFile(src, dst string) error {
	input, err := os.Open(src)
	if err != nil {
		return err
	}
	defer input.Close()

	output, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer output.Close()

	_, err = io.Copy(output, input)
	return err
}

func exitOnError(msg string, err error) {
	if err != nil {
		fmt.Printf("%s: %s\n", msg, err)
		os.Exit(1)
	}
}
