package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"image/png"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/jsonfmt"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"

	"github.com/spf13/cobra"
)

// AssemblyScript 0.28+ requires Node ≥ 20.
const minNodeMajor = 20

// Where a build is assembled before it becomes build/.
const outDir = "build.next"

// The scripts' npm package, relative to the project root.
const scriptsPkgDir = "assets/scripts"

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build game assets and compile AssemblyScript",
	Long: `Builds the project into build/: compiles scripts, bakes atlases and
tilesets, and checks scenes and prefabs against the engine's schema.

--json prints no progress; each problem is a JSON line ({"level", "category",
"message", "file", "line", "column"}) and the last line is the result
({"result": "ok"|"failed", "errors", "warnings"}).`,
	Run: func(cmd *cobra.Command, args []string) {
		say("Building game...")

		projectRoot, err := os.Getwd()
		exitOnError("Failed to resolve project root", err)

		// Toolchain errors are already actionable; print them as-is.
		if err := checkBuildPrereqs(projectRoot); err != nil {
			fail(Diagnostic{Category: "toolchain", Message: err.Error()})
		}

		man, err := manifest.LoadManifest(archive.ManifestEntryKey)
		exitOnError("Error loading manifest", err)

		// npm install prunes @jm/runtime and libraries (they aren't in package.json), so re-extract them.
		exitOnError("Failed to sync script packages", syncScriptPackages(projectRoot, man))

		man.Assets, err = manifest.ExpandAssets(os.DirFS(projectRoot), man.Assets)
		exitOnError("Failed to expand asset patterns", err)

		// Validate every path before touching the filesystem.
		for _, p := range slices.Concat(man.Assets, man.Scenes) {
			exitOnError(fmt.Sprintf("Invalid manifest path %q", p), validateRelativePath(p))
			if strings.HasSuffix(p, ".script.json") {
				fail(Diagnostic{Category: "manifest", File: p, Message: "a legacy .script.json asset; run `jm migrate` to convert the project"})
			}
		}

		// build/ is CLI-owned and starts empty so stale artifacts never ship. The new
		// build goes to a staging folder that replaces build/ only once it's all there,
		// so a failed build (a script that doesn't compile) leaves the last good one
		// in place for the editor and the game.
		exitOnError("Failed to clean the staging directory", os.RemoveAll(outDir))

		exitOnError("Failed to write the built manifest",
			editManifest(archive.ManifestEntryKey, filepath.Join(outDir, archive.ManifestEntryKey),
				func(raw map[string]any) { raw["assets"] = man.Assets }))

		for _, asset := range man.Assets {
			exitOnError("Failed to copy "+asset, copyFile(asset, filepath.Join(outDir, asset)))
			say("Copied asset: %s", asset)
			if strings.HasSuffix(asset, ".ts") {
				if err := runAsc(asset, projectRoot); err != nil {
					fail(Diagnostic{Category: "script", File: asset, Message: "doesn't compile (asc: " + err.Error() + ")"})
				}
				say("Built script: %s", asset)
			}
		}
		for _, asset := range man.Assets {
			if strings.HasSuffix(asset, ".atlas.json") {
				if err := bakeAtlas(asset); err != nil {
					fail(Diagnostic{Category: "atlas", File: asset, Message: err.Error()})
				}
			}
			if strings.HasSuffix(asset, ".tsj") {
				if err := bakeTileset(asset); err != nil {
					fail(Diagnostic{Category: "tileset", File: asset, Message: err.Error()})
				}
			}
		}
		for _, scene := range man.Scenes {
			exitOnError("Failed to copy "+scene, copyFile(scene, filepath.Join(outDir, scene)))
		}

		content := slices.Clone(man.Scenes)
		for _, f := range man.Assets {
			if strings.HasSuffix(f, ".prefab.json") {
				content = append(content, f)
			}
		}
		checkContent(man.EnginePath, slices.Compact(slices.Sorted(slices.Values(content))))

		exitOnError("Failed to replace build/", swapBuild())
		say("Build complete!")
		finish(true)
	},
}

func init() {
	buildCmd.Flags().BoolVar(&jsonOutput, "json", false, "problems as JSON lines, no progress (for tools)")
}

// swapBuild makes the finished staging folder the build.
func swapBuild() error {
	if err := os.RemoveAll("build.old"); err != nil {
		return err
	}
	if err := os.Rename("build", "build.old"); err != nil && !os.IsNotExist(err) {
		return err
	}
	if err := os.Rename(outDir, "build"); err != nil {
		return err
	}
	return os.RemoveAll("build.old")
}

// scriptsPath joins elem onto the project's scripts package folder.
func scriptsPath(projectRoot string, elem ...string) string {
	return filepath.Join(append([]string{projectRoot, filepath.FromSlash(scriptsPkgDir)}, elem...)...)
}

// checkBuildPrereqs verifies Node and the project's npm install (running it on
// a new project's first build), returning an error that says how to fix it.
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

	if _, err := os.Stat(scriptsPath(projectRoot, "node_modules")); os.IsNotExist(err) {
		if _, err := exec.LookPath("npm"); err != nil {
			return fmt.Errorf("npm dependencies not installed and npm isn't on PATH. Install Node.js, then: cd %s && npm install", scriptsPkgDir)
		}
		say("Installing script dependencies (first build)...")
		install := exec.Command("npm", "install", "--no-audit", "--no-fund")
		install.Dir = scriptsPath(projectRoot)
		install.Stdout = os.Stdout
		if jsonOutput {
			install.Stdout = os.Stderr // stdout is for the JSON lines
		}
		install.Stderr = os.Stderr
		if err := install.Run(); err != nil {
			return fmt.Errorf("npm install in %s failed: %w", scriptsPkgDir, err)
		}
	}
	if _, err := os.Stat(scriptsPath(projectRoot, "node_modules", "assemblyscript")); err != nil {
		return fmt.Errorf("AssemblyScript missing from %s/node_modules (%v). Run: cd %s && npm install", scriptsPkgDir, err, scriptsPkgDir)
	}
	return nil
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
	var major int
	if _, err := fmt.Sscanf(strings.TrimPrefix(raw, "v"), "%d", &major); err != nil {
		return 0, raw, fmt.Errorf("unparseable node version: %q", raw)
	}
	return major, raw, nil
}

// syncScriptPackages puts @jm/runtime and the manifest's script libraries in
// the scripts' node_modules, so scripts (and tests) can import them.
func syncScriptPackages(projectRoot string, m manifest.GameManifest) error {
	if err := syncEmbeddedRuntime(projectRoot); err != nil {
		return fmt.Errorf("@jm/runtime: %w", err)
	}
	for name, dir := range m.ScriptLibraries {
		if err := syncLibrary(projectRoot, name, dir); err != nil {
			return fmt.Errorf("script library %s: %w", name, err)
		}
	}
	return nil
}

// syncEmbeddedRuntime replaces node_modules/@jm/runtime with the copy embedded
// in this jm binary.
func syncEmbeddedRuntime(projectRoot string) error {
	runtime, err := fs.Sub(stdlib.StdLibFiles, "runtime")
	if err != nil {
		return err
	}
	return copyTree(runtime, scriptsPath(projectRoot, "node_modules", "@jm", "runtime"))
}

// packageName matches npm package names: "name" or "@scope/name", lowercase,
// not starting with "." or "_" (so never "..", "." or a path).
var packageName = regexp.MustCompile(`^(@[a-z0-9~-][a-z0-9._~-]*/)?[a-z0-9~-][a-z0-9._~-]*$`)

// syncLibrary copies a shared script folder (relative to the project, e.g.
// "../common") to node_modules/<name>, giving it a package.json if it has none.
func syncLibrary(projectRoot, name, dir string) error {
	// The name becomes a folder that is wiped and refilled.
	if !packageName.MatchString(name) || name == "@jm/runtime" {
		return fmt.Errorf("%q is not a package name like \"common\" or \"@demos/common\"", name)
	}
	src := filepath.Join(projectRoot, dir)
	if info, err := os.Stat(src); err != nil || !info.IsDir() {
		return fmt.Errorf("folder %s not found", src)
	}
	dst := scriptsPath(projectRoot, "node_modules", filepath.FromSlash(name))
	if err := copyTree(os.DirFS(src), dst); err != nil {
		return err
	}
	body := fmt.Sprintf("{\n  \"name\": %q,\n  \"main\": \"index.ts\",\n  \"private\": true\n}\n", name)
	_, err := writeIfMissing(filepath.Join(dst, "package.json"), []byte(body))
	return err
}

// copyTree replaces dst with a copy of fsys, leaving out node_modules folders
// (wiping first, so removed files don't linger).
func copyTree(fsys fs.FS, dst string) error {
	if err := os.RemoveAll(dst); err != nil {
		return err
	}
	return fs.WalkDir(fsys, ".", func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		out := filepath.Join(dst, filepath.FromSlash(path))
		if d.IsDir() {
			if d.Name() == "node_modules" {
				return filepath.SkipDir
			}
			return os.MkdirAll(out, 0755)
		}
		data, err := fs.ReadFile(fsys, path)
		if err != nil {
			return err
		}
		return os.WriteFile(out, data, 0644)
	})
}

// editManifest round-trips the manifest at src through a generic map, so
// fields this CLI doesn't model survive, applies edit and writes it to dst.
func editManifest(src, dst string, edit func(raw map[string]any)) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	var raw map[string]any
	if err := json.Unmarshal(data, &raw); err != nil {
		return err
	}
	edit(raw)
	edited, err := json.Marshal(raw)
	if err != nil {
		return err
	}
	out, err := jsonfmt.FormatKeeping(edited, data) // the file's key order, not the map's
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}
	return os.WriteFile(dst, out, 0644)
}

// bakeAtlas packs an .atlas.json's source PNGs (read from the project, not
// shipped) into <name>.atlas.png in the staging build and rewrites the json
// there with the regions.
func bakeAtlas(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	cfg, err := atlas.LoadConfig(data)
	if err != nil {
		return fmt.Errorf("parse: %w", err)
	}
	if len(cfg.Sources) == 0 {
		return fmt.Errorf("missing or empty 'sources'")
	}
	sources, err := loadAtlasSources(cfg.Sources)
	if err != nil {
		return err
	}
	maxSize := cfg.MaxSize
	if maxSize <= 0 {
		maxSize = 4096 // the smallest GL_MAX_TEXTURE_SIZE OpenGL 4.1 allows
	}
	img, regions, err := atlas.Pack(sources, cfg.Padding, maxSize)
	if err != nil {
		return fmt.Errorf("pack: %w", err)
	}
	if cfg.Filter != "linear" {
		cfg.Filter = "nearest" // the default, and the fallback for unknown filters
	}

	// Build-root-relative with forward slashes: the key the archive uses.
	imageRel := filepath.ToSlash(filepath.Clean(strings.TrimSuffix(path, ".atlas.json") + ".atlas.png"))
	var pngData bytes.Buffer
	if err := png.Encode(&pngData, img); err != nil {
		return fmt.Errorf("encode png: %w", err)
	}
	if err := os.WriteFile(filepath.Join(outDir, imageRel), pngData.Bytes(), 0o644); err != nil {
		return err
	}
	out := atlas.AtlasOutput{
		Image: imageRel, Width: img.Bounds().Dx(), Height: img.Bounds().Dy(),
		Filter: cfg.Filter, Regions: regions,
	}
	outBytes, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(outDir, path), outBytes, 0o644); err != nil {
		return err
	}
	say("Atlas: %s (%dx%d, %d regions)", path, out.Width, out.Height, len(out.Regions))
	return nil
}

// loadAtlasSources decodes project-relative PNGs, naming each region after
// its file name without extension; two files may not share a name.
func loadAtlasSources(paths []string) ([]atlas.SourceImage, error) {
	out := make([]atlas.SourceImage, 0, len(paths))
	seen := map[string]string{} // region name → the path that produced it
	for _, p := range paths {
		if err := validateRelativePath(p); err != nil {
			return nil, fmt.Errorf("atlas source: %w", err)
		}
		base := filepath.Base(p)
		name := strings.TrimSuffix(base, filepath.Ext(base))
		if prior, dup := seen[name]; dup {
			return nil, fmt.Errorf("sources %q and %q both produce region name %q", prior, p, name)
		}
		seen[name] = p
		f, err := os.Open(p)
		if err != nil {
			return nil, err
		}
		img, err := png.Decode(f)
		f.Close()
		if err != nil {
			return nil, fmt.Errorf("decode %q: %w", p, err)
		}
		out = append(out, atlas.SourceImage{Name: name, Img: img})
	}
	return out, nil
}

// entryTemplate wraps a user script so its exports can use runtime types:
// the engine calls onCollide(index, generation) and __jmOnMessage(); the script
// receives an Entity and a Message. Every hook is optional in the script.
const entryTemplate = `// Generated by jm build; do not edit.
import * as script from "%s";
import { Entity, Message } from "@jm/runtime";

export function onUpdate(dt: f32): void {
  if (isDefined(script.onUpdate)) script.onUpdate(dt);
}

export function onCollide(index: u32, generation: u32): void {
  if (isDefined(script.onCollide)) script.onCollide(new Entity(index, generation));
}

export function __jmOnMessage(): void {
  if (isDefined(script.onMessage)) script.onMessage(Message.current());
}
`

// runAsc compiles a script (project-relative path) over its source copy in the
// staging build, through a generated entry under node_modules/.jm so
// @jm/runtime resolves normally.
func runAsc(scriptPath, projectRoot string) error {
	scriptsDir := scriptsPath(projectRoot)
	entry := scriptsPath(projectRoot, "node_modules", ".jm", "entries", scriptPath)
	importPath, err := filepath.Rel(filepath.Dir(entry), filepath.Join(projectRoot, strings.TrimSuffix(scriptPath, ".ts")))
	if err != nil {
		return fmt.Errorf("compute entry import path: %w", err)
	}
	if err := os.MkdirAll(filepath.Dir(entry), 0755); err != nil {
		return err
	}
	if err := os.WriteFile(entry, []byte(fmt.Sprintf(entryTemplate, filepath.ToSlash(importPath))), 0644); err != nil {
		return err
	}
	entryRel, _ := filepath.Rel(scriptsDir, entry)
	// The project's own compiler (checkBuildPrereqs made sure it is installed),
	// run by node directly: npx would fetch one from the network if it weren't.
	// --optimize halves both a script's start (each spawn of a scripted entity)
	// and its onUpdate, measured on Strike Wing.
	asc := filepath.Join("node_modules", "assemblyscript", "bin", "asc.js")
	cmd := exec.Command("node", asc, filepath.ToSlash(entryRel), "--config", "asconfig.json", "--optimize",
		"--outFile", filepath.Join(projectRoot, outDir, scriptPath))
	cmd.Dir = scriptsDir
	if !jsonOutput {
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		return cmd.Run()
	}
	// --json: asc's messages become diagnostics with project paths.
	var output bytes.Buffer
	cmd.Stdout, cmd.Stderr = &output, &output
	err = cmd.Run()
	for _, d := range parseAsc(output.String(), func(p string) string {
		if rel, err := filepath.Rel(projectRoot, filepath.Join(scriptsDir, p)); err == nil {
			return filepath.ToSlash(rel)
		}
		return p
	}) {
		emit(d)
	}
	return err
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
	if slices.Contains(strings.Split(filepath.ToSlash(filepath.Clean(p)), "/"), "..") {
		return fmt.Errorf("path traversal not allowed: %s", p)
	}
	return nil
}

func copyFile(src, dst string) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}
	return os.WriteFile(dst, data, 0644)
}

func exitOnError(msg string, err error) {
	if err != nil {
		fail(Diagnostic{Category: "build", Message: fmt.Sprintf("%s: %s", msg, err)})
	}
}
