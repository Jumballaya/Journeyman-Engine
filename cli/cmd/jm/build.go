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

	"github.com/spf13/cobra"
)

// AssemblyScript 0.28+ requires Node ≥ 20.
const minNodeMajor = 20

// The scripts' npm package, relative to the project root.
const scriptsPkgDir = "assets/scripts"

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build game assets and compile AssemblyScript",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("Building game...")

		projectRoot, err := os.Getwd()
		exitOnError("Failed to resolve project root", err)

		// Toolchain errors are already actionable; print them as-is.
		if err := checkBuildPrereqs(projectRoot); err != nil {
			fmt.Println(err)
			os.Exit(1)
		}

		// npm install prunes @jm/runtime (it isn't in package.json), so re-extract it.
		exitOnError("Failed to sync @jm/runtime", syncEmbeddedRuntime(projectRoot))

		manifestData, err := manifest.LoadManifest(archive.ManifestEntryKey)
		exitOnError("Error loading manifest", err)

		// Validate every path before touching the filesystem.
		for _, p := range manifestData.Assets {
			if err := validateRelativePath(p); err != nil {
				exitOnError(fmt.Sprintf("Invalid manifest asset path %q", p), err)
			}
			if strings.HasSuffix(p, ".script.json") {
				fmt.Printf("%s is a legacy .script.json asset; run `jm migrate` to convert the project.\n", p)
				os.Exit(1)
			}
		}
		for _, p := range manifestData.Scenes {
			if err := validateRelativePath(p); err != nil {
				exitOnError(fmt.Sprintf("Invalid manifest scene path %q", p), err)
			}
		}

		// build/ is CLI-owned; start empty so stale artifacts never ship.
		exitOnError("Failed to clean build directory", os.RemoveAll("build"))

		copyFileOrExit(archive.ManifestEntryKey, filepath.Join("build", archive.ManifestEntryKey))

		processAssets(manifestData.Assets, projectRoot)
		processAtlases(manifestData.Assets)
		for _, scene := range manifestData.Scenes {
			copyFileOrExit(scene, filepath.Join("build", scene))
		}

		fmt.Println("Build complete!")
	},
}

// checkBuildPrereqs verifies Node and the project's npm install, returning an
// error that says how to fix the specific problem.
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

// syncEmbeddedRuntime replaces node_modules/@jm/runtime with the copy embedded
// in this jm binary (wiping first, so removed files don't linger).
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

// nodeMajorVersion parses `node --version` ("v20.10.0", "v18.17.1-pre"); the
// timeout keeps a hung shim on PATH from stalling the build.
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

func processAssets(assets []string, projectRoot string) {
	for _, asset := range assets {
		dst := filepath.Join("build", asset)
		copyFileOrExit(asset, dst)
		fmt.Printf("Copied asset: %s\n", asset)

		if strings.HasSuffix(asset, ".ts") {
			buildScript(asset, projectRoot)
		}
	}
}

// processAtlases packs each .atlas.json's source PNGs (read from the project,
// not shipped) into build/<name>.atlas.png and rewrites the json with regions.
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

		err = checkUniqueBasenames(cfg.Sources)
		exitOnError(fmt.Sprintf("atlas: %s", a), err)

		srcImgs, err := loadAtlasSources(cfg.Sources)
		exitOnError(fmt.Sprintf("atlas: %s: load sources", a), err)

		atlasImg, regions, err := atlas.Pack(srcImgs, cfg.Padding, atlasMaxOrDefault(cfg))
		exitOnError(fmt.Sprintf("atlas: %s: pack", a), err)

		atlasJsonOut := filepath.Join("build", a)
		atlasPngOut := strings.TrimSuffix(atlasJsonOut, ".atlas.json") + ".atlas.png"
		err = os.MkdirAll(filepath.Dir(atlasPngOut), 0o755)
		exitOnError(fmt.Sprintf("atlas: %s: mkdir", a), err)

		err = writeAtlasPng(atlasPngOut, atlasImg)
		exitOnError(fmt.Sprintf("atlas: %s: write png", a), err)

		// Build-root-relative with forward slashes: the key the archive uses.
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

// loadAtlasSources decodes project-relative PNGs, naming each region after
// its file name without extension.
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

// checkUniqueBasenames rejects two sources that would share a region name
// within one atlas, naming both files.
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

// writeAtlasPng output is deterministic for a given Go version.
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

// Defaults to 4096, the smallest GL_MAX_TEXTURE_SIZE OpenGL 4.1 allows.
func atlasMaxOrDefault(cfg atlas.AtlasConfig) int {
	if cfg.MaxSize <= 0 {
		return 4096
	}
	return cfg.MaxSize
}

// Unrecognized filters fall back to "nearest" (pixel art).
func filterOrDefault(s string) string {
	switch s {
	case "nearest", "linear":
		return s
	default:
		return "nearest"
	}
}

// buildScript compiles a .ts script; the wasm replaces the source copy at the
// same path under build/, which is where the engine looks for it.
func buildScript(tsPath, projectRoot string) {
	if err := validateRelativePath(tsPath); err != nil {
		exitOnError(fmt.Sprintf("Invalid script path %s", tsPath), err)
	}
	if err := runAsc(tsPath, filepath.Join("build", tsPath), projectRoot); err != nil {
		exitOnError(fmt.Sprintf("asc failed for %s", tsPath), err)
	}
	fmt.Printf("Built script: %s\n", tsPath)
}

// entryTemplate wraps a user script so its exports can use runtime types:
// the engine calls onCollide(index, generation); the script receives an Entity.
// Both hooks are optional in the script.
const entryTemplate = `// Generated by jm build; do not edit.
import * as script from "%s";
import { Entity } from "@jm/runtime";

export function onUpdate(dt: f32): void {
  if (isDefined(script.onUpdate)) script.onUpdate(dt);
}

export function onCollide(index: u32, generation: u32): void {
  if (isDefined(script.onCollide)) script.onCollide(new Entity(index, generation));
}
`

// runAsc compiles a script (manifest-relative path) to outputPath through a
// generated entry under node_modules/.jm, so @jm/runtime resolves normally.
func runAsc(scriptPath, outputPath, projectRoot string) error {
	scriptsDir := filepath.Join(projectRoot, scriptsPkgDir)
	relScript, err := filepath.Rel(scriptsDir, filepath.Join(projectRoot, scriptPath))
	if err != nil {
		return fmt.Errorf("compute relative script path: %w", err)
	}

	entry := filepath.Join(scriptsDir, "node_modules", ".jm", "entries", relScript)
	importPath, err := filepath.Rel(filepath.Dir(entry), filepath.Join(scriptsDir, strings.TrimSuffix(relScript, ".ts")))
	if err != nil {
		return fmt.Errorf("compute entry import path: %w", err)
	}
	if err := os.MkdirAll(filepath.Dir(entry), 0755); err != nil {
		return fmt.Errorf("mkdir %s: %w", filepath.Dir(entry), err)
	}
	source := fmt.Sprintf(entryTemplate, filepath.ToSlash(importPath))
	if err := os.WriteFile(entry, []byte(source), 0644); err != nil {
		return fmt.Errorf("write entry %s: %w", entry, err)
	}

	outputAbs := filepath.Join(projectRoot, outputPath)
	if err := os.MkdirAll(filepath.Dir(outputAbs), 0755); err != nil {
		return fmt.Errorf("mkdir %s: %w", filepath.Dir(outputAbs), err)
	}
	entryRel, _ := filepath.Rel(scriptsDir, entry)
	cmd := exec.Command("npx", "asc", filepath.ToSlash(entryRel), "--config", "asconfig.json", "--outFile", outputAbs)
	cmd.Dir = scriptsDir
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

// validateRelativePath keeps manifest paths inside the project: no empty,
// absolute, drive/UNC-rooted or `..`-segment paths (`foo..bar` is fine).
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
