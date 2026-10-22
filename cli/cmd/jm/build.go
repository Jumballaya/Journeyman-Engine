package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"image"
	"image/png"
	"io"
	"io/fs"
	"maps"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/art"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/formats"
	"github.com/Jumballaya/Journeyman-Engine/internal/jsonfmt"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/schema"
	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"
	"github.com/Jumballaya/Journeyman-Engine/internal/toolchain"
	"github.com/spf13/cobra"
)

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
	RunE: func(cmd *cobra.Command, args []string) error { return build() },
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

// scriptToolchain finds Node and AssemblyScript for the project's scripts,
// downloading them on first use when the machine or project has none.
func scriptToolchain(projectRoot string) (toolchain.Toolchain, error) {
	var log io.Writer = os.Stdout
	if jsonOutput {
		log = os.Stderr // stdout is for the JSON lines
	}
	return toolchain.Find(scriptsPath(projectRoot), true, log)
}

// syncScriptPackages puts @jm/runtime and the manifest's script libraries in
// the scripts' node_modules, so scripts (and tests) can import them.
func syncScriptPackages(projectRoot string, m manifest.GameManifest, tc toolchain.Toolchain) error {
	// Not in a sandbox: the link (for editors only) would lead out of it.
	if _, kind := copyOf(projectRoot); kind != "sandboxes" {
		tc.LinkInto(scriptsPath(projectRoot))
	}
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

// syncEmbeddedRuntime replaces node_modules/@jm/runtime and @jm/behaviors with
// the copies embedded in this jm binary.
func syncEmbeddedRuntime(projectRoot string) error {
	for _, pkg := range []string{"runtime", "behaviors"} {
		files, err := fs.Sub(stdlib.StdLibFiles, pkg)
		if err != nil {
			return err
		}
		if err := copyTree(files, scriptsPath(projectRoot, "node_modules", "@jm", pkg)); err != nil {
			return err
		}
	}
	return nil
}

// A stock behavior's script name ("@jm/behaviors/trigger.ts"): content attaches
// it like a project script; jm build compiles it from node_modules.
const behaviorsPrefix = "@jm/behaviors/"

// packageName matches npm package names: "name" or "@scope/name", lowercase,
// not starting with "." or "_" (so never "..", "." or a path).
var packageName = regexp.MustCompile(`^(@[a-z0-9~-][a-z0-9._~-]*/)?[a-z0-9~-][a-z0-9._~-]*$`)

// syncLibrary copies a shared script folder (relative to the project, e.g.
// "../common") to node_modules/<name>, giving it a package.json if it has none.
// librarySource is the folder a script library (the manifest's
// scriptLibraries) is copied from. A sandbox or variant is a copy of the
// game's own folder: a library beside the game ("../common") is where the game
// finds it, not beside the copy.
func librarySource(projectRoot, dir string) string {
	if game, _ := copyOf(projectRoot); game != "" && !filepath.IsAbs(dir) && !filepath.IsLocal(dir) {
		return filepath.Join(game, dir)
	}
	return filepath.Join(projectRoot, dir)
}

func syncLibrary(projectRoot, name, dir string) error {
	// The name becomes a folder that is wiped and refilled.
	if !packageName.MatchString(name) {
		return fmt.Errorf("%q is not a package name like \"common\" or \"@demos/common\"", name)
	}
	if strings.HasPrefix(name, "@jm/") { // jm's own (@jm/runtime, @jm/behaviors): never replaced
		return fmt.Errorf("%q is reserved for jm's packages: name the library something else", name)
	}
	src := librarySource(projectRoot, dir)
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
	return atomicfile.WriteFile(dst, out, 0644)
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
	for _, p := range cfg.Sources {
		if err := validateRelativePath(p); err != nil {
			return fmt.Errorf("atlas source: %w", err)
		}
	}
	sources, clips, err := cfg.Read(func(p string) ([]byte, error) { return os.ReadFile(filepath.FromSlash(p)) }, 0)
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
	if err := writeBuiltPNG(imageRel, img); err != nil {
		return err
	}
	out := atlas.AtlasOutput{
		Image: imageRel, Width: img.Bounds().Dx(), Height: img.Bounds().Dy(),
		Filter: cfg.Filter, Regions: regions, Clips: clips,
	}
	if normals := atlas.PackNormals(sources, regions, img.Bounds()); normals != nil {
		out.NormalImage = atlas.NormalPath(imageRel)
		if err := writeBuiltPNG(out.NormalImage, normals); err != nil {
			return err
		}
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

// writeBuiltPNG encodes img into the staging build at the build-relative rel.
func writeBuiltPNG(rel string, img image.Image) error {
	var data bytes.Buffer
	if err := png.Encode(&data, img); err != nil {
		return fmt.Errorf("encode png: %w", err)
	}
	return os.WriteFile(filepath.Join(outDir, rel), data.Bytes(), 0o644)
}

// withNormalMaps adds each image asset's normal map (atlas.NormalPath) when the
// project has one: the engine loads it by convention, so it ships unlisted.
func withNormalMaps(assets []string) []string {
	out := slices.Clone(assets)
	for _, a := range assets {
		if f, _ := formats.Of(strings.ToLower(a)); f.Archive != "image" || atlas.IsNormalMap(a) {
			continue
		}
		if n := atlas.NormalPath(a); !slices.Contains(out, n) && fileExists(n) {
			out = append(out, n)
		}
	}
	return out
}

func fileExists(p string) bool {
	info, err := os.Stat(p)
	return err == nil && !info.IsDir()
}

// entryTemplate wraps a user script so its exports can use runtime types:
// the engine calls each physics hook with (index, generation) and __jmOnMessage();
// the script receives an Entity (or nothing, for ground hooks) and a Message.
// Every hook is optional in the script.
const entryTemplate = `// Generated by jm build; do not edit.
import * as script from "%s";
import { Entity, Message } from "@jm/runtime";

export function onUpdate(dt: f32): void {
  if (isDefined(script.onUpdate)) script.onUpdate(dt);
}

export function onOverlap(index: u32, generation: u32): void {
  if (isDefined(script.onOverlap)) script.onOverlap(new Entity(index, generation));
}

export function onOverlapStart(index: u32, generation: u32): void {
  if (isDefined(script.onOverlapStart)) script.onOverlapStart(new Entity(index, generation));
}

export function onOverlapEnd(index: u32, generation: u32): void {
  if (isDefined(script.onOverlapEnd)) script.onOverlapEnd(new Entity(index, generation));
}

export function onLanded(index: u32, generation: u32): void {
  if (isDefined(script.onLanded)) script.onLanded();
}

export function onLeftGround(index: u32, generation: u32): void {
  if (isDefined(script.onLeftGround)) script.onLeftGround();
}

export function onDestroy(): void {
  if (isDefined(script.onDestroy)) script.onDestroy();
}

export function __jmOnMessage(): void {
  if (isDefined(script.onMessage)) script.onMessage(Message.current());
}
`

// runAsc compiles a script (project-relative path) to outFile, through a
// generated entry under node_modules/.jm so @jm/runtime resolves normally.
func runAsc(r *report, tc toolchain.Toolchain, scriptPath, projectRoot, outFile string) error {
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
	// Run by node directly, not npx (which would fetch from the network).
	// --optimize halves both a script's start (each spawn of a scripted entity)
	// and its onUpdate, measured on Strike Wing.
	asc := filepath.Join(tc.ASC, "bin", "asc.js")
	cmd := exec.Command(tc.Node, asc, filepath.ToSlash(entryRel), "--config", "asconfig.json", "--optimize",
		"--outFile", outFile)
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
		r.add(d)
	}
	return err
}

// scriptNameProblems are warnings for literal prefab and scene names in
// scripts that no listed file answers to: a typo found at build, not at spawn.
func scriptNameProblems(man manifest.GameManifest) []Diagnostic {
	suffixes := map[string]string{"prefab": ".prefab.json", "scene": ".scene.json"}
	var problems []Diagnostic
	listed := slices.Concat(man.Scenes, man.Assets)
	for _, src := range scriptSources(man) {
		for _, ref := range src.refs() {
			suffix := suffixes[ref.kind]
			if suffix == "" || !ref.exact {
				continue // "pickup_" + kind: a name made at run time
			}
			var names []string
			found := false
			for _, p := range listed {
				if !strings.HasSuffix(p, suffix) {
					continue
				}
				short := strings.TrimSuffix(path.Base(p), suffix)
				names = append(names, short)
				found = found || p == ref.name || short == ref.name
			}
			if !found {
				line, col := src.at(ref.offset)
				problems = append(problems, Diagnostic{Level: "warning", Category: "script", File: src.file, Line: line, Column: col,
					Message: fmt.Sprintf("no %s named %q in .jm.json%s", ref.kind, ref.name, schema.Suggest(ref.name, names))})
			}
		}
	}
	return problems
}

// inputCall finds an Input call; actionArgs is how many of its leading string
// arguments name actions (bind's first defines one).
var (
	inputCall  = regexp.MustCompile(`\bInput\.(down|justPressed|justReleased|value|repeated|axis|vector|bind)\(`)
	stringArg  = regexp.MustCompile(`^\s*"([^"]*)"\s*([,)]?)`)
	actionArgs = map[string]int{"down": 1, "justPressed": 1, "justReleased": 1, "value": 1, "repeated": 1, "axis": 2, "vector": 4, "bind": 1}
)

// inputActionProblems are warnings for actions scripts read that no
// .bindings.json defines (and no script binds): they'd read as never pressed.
func inputActionProblems(man manifest.GameManifest) []Diagnostic {
	defined := map[string]bool{}
	bindings := false
	for _, file := range man.Assets {
		if !strings.HasSuffix(file, ".bindings.json") {
			continue
		}
		var doc struct {
			Actions map[string]any `json:"actions"`
		}
		if data, err := os.ReadFile(file); err == nil && json.Unmarshal(data, &doc) == nil {
			bindings = true
			for name := range doc.Actions {
				defined[name] = true
			}
		}
	}
	if !bindings {
		return nil
	}
	type use struct {
		src    scriptSource
		name   string
		offset int
	}
	var uses []use
	for _, src := range scriptSources(man) {
		for _, m := range inputCall.FindAllStringSubmatchIndex(src.code, -1) {
			method, at := src.text[m[2]:m[3]], m[1]
			for n := 0; n < actionArgs[method]; n++ {
				arg := stringArg.FindStringSubmatchIndex(src.text[at:])
				if arg != nil && arg[4] == arg[5] {
					arg = nil // "move_" + side: a name made at run time
				}
				if arg == nil {
					if method == "bind" {
						return nil // an action bound by a name made at run time: can't tell what's defined
					}
					break
				}
				name := src.text[at+arg[2] : at+arg[3]]
				if method == "bind" {
					defined[name] = true
				} else {
					uses = append(uses, use{src, name, at + arg[2]})
				}
				at += arg[1]
			}
		}
	}
	names := make([]string, 0, len(defined))
	for name := range defined {
		names = append(names, name)
	}
	var problems []Diagnostic
	for _, u := range uses {
		if !defined[u.name] {
			line, col := u.src.at(u.offset)
			problems = append(problems, Diagnostic{Level: "warning", Category: "script", File: u.src.file, Line: line, Column: col,
				Message: fmt.Sprintf("no input action %q in a .bindings.json (it reads as never pressed)%s", u.name, schema.Suggest(u.name, names))})
		}
	}
	return problems
}

// scriptPathLiteral is a quoted string naming a .ts file, in a script's source:
// spawn overrides can attach a script ("ScriptComponent", "script", "x.ts").
var scriptPathLiteral = regexp.MustCompile(`["'\x60]([^"'\x60\s]+\.ts)["'\x60]`)

// referencedScripts collects the scripts something attaches: every string
// ending in .ts in the JSON content files among paths (scenes, prefabs, maps,
// data) and every .ts path literal in the scripts' code among them.
func referencedScripts(paths []string) map[string]bool {
	found := map[string]bool{}
	var walk func(v any)
	walk = func(v any) {
		switch v := v.(type) {
		case string:
			if strings.HasSuffix(v, ".ts") {
				found[filepath.ToSlash(filepath.Clean(v))] = true
			}
		case []any:
			for _, item := range v {
				walk(item)
			}
		case map[string]any:
			for _, item := range v {
				walk(item)
			}
		}
	}
	for _, p := range paths {
		if strings.HasSuffix(p, ".ts") {
			if data, err := os.ReadFile(p); err == nil {
				src := newScriptSource(p, string(data))
				for _, m := range scriptPathLiteral.FindAllStringSubmatchIndex(src.text, -1) {
					if src.inCode(m[0]) { // a literal, not a comment's or another string's words
						walk(src.text[m[2]:m[3]])
					}
				}
			}
			continue
		}
		if !strings.HasSuffix(p, ".json") && !strings.HasSuffix(p, ".tmj") && !strings.HasSuffix(p, ".tsj") {
			continue
		}
		data, err := os.ReadFile(p)
		if err != nil {
			continue // missing files are reported where they're used
		}
		var v any
		if json.Unmarshal(data, &v) == nil {
			walk(v)
		}
	}
	return found
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

// copyOf gives the game a sandbox's or variant's folder copies
// (<game>/.jm/sandboxes/<name>, <game>/.jm/variants/<name>) and which kind it
// is ("sandboxes", "variants"); "" for a game's own folder.
func copyOf(root string) (game, kind string) {
	kinds := filepath.Dir(filepath.Clean(root))
	if k := filepath.Base(kinds); (k != "sandboxes" && k != "variants") || filepath.Base(filepath.Dir(kinds)) != ".jm" {
		return "", ""
	}
	game = filepath.Dir(filepath.Dir(kinds))
	if _, err := os.Stat(filepath.Join(game, ".jm.json")); err != nil {
		return "", ""
	}
	return game, filepath.Base(kinds)
}

// build is jm build: the game in the working folder built into build/, its
// problems reported, failed (exit 1, said already) on an error.
func build() error {
	r := &report{category: "build"}
	return r.end(buildGame(r))
}

// buildGame builds the game in the working folder into build/, reporting its problems to r.
func buildGame(r *report) error {
	say("Building game...")
	stage("Building")

	projectRoot, err := os.Getwd()
	if err != nil {
		return fmt.Errorf("Failed to resolve project root: %w", err)
	}
	warnAboutEditorEdits(r, projectRoot)

	// Toolchain errors are already actionable; print them as-is.
	tc, err := scriptToolchain(projectRoot)
	if err != nil {
		return Diagnostic{Category: "toolchain", Message: err.Error()}
	}

	sources := buildSources(projectRoot) // before reading any: one changed meanwhile makes this build stale
	man, err := manifest.LoadManifest(archive.ManifestEntryKey)
	if err != nil {
		return fmt.Errorf("Error loading manifest: %w", err)
	}

	// npm install prunes @jm/runtime and libraries (they aren't in package.json), so re-extract them.
	if err := syncScriptPackages(projectRoot, man, tc); err != nil {
		return fmt.Errorf("Failed to sync script packages: %w", err)
	}

	man.Assets, err = manifest.ExpandAssets(os.DirFS(projectRoot), man.Assets)
	if err != nil {
		return fmt.Errorf("Failed to expand asset patterns: %w", err)
	}
	man.Assets = withNormalMaps(man.Assets)

	// Validate every path before touching the filesystem.
	for _, p := range slices.Concat(man.Assets, man.Scenes) {
		if err := validateRelativePath(p); err != nil {
			return fmt.Errorf("Invalid manifest path %q: %w", p, err)
		}
		if strings.HasSuffix(p, ".script.json") {
			return Diagnostic{Category: "manifest", File: p, Message: "a legacy .script.json asset; run `jm migrate` to convert the project"}
		}
	}

	// A .ts asset is a script when content attaches it (a ScriptComponent in
	// a scene, prefab, map or data file names it, or the manifest does). The rest are modules that
	// scripts import (or scripts not attached yet): compiled, so their errors
	// show, but not shipped on their own.
	for _, d := range slices.Concat(scriptNameProblems(man), inputActionProblems(man)) {
		r.add(d)
	}
	// The art keeps the game's art note (design/Art.md): its palette, sizes and animation sets.
	for _, f := range art.Check(projectRoot).Findings {
		r.add(Diagnostic{Level: "warning", Category: "art", File: f.File, Message: f.Message})
	}
	// The manifest too: a dedicated server's scripts are in net.server.scripts.
	scripts := referencedScripts(slices.Concat([]string{archive.ManifestEntryKey}, man.Scenes, man.Assets))
	for _, script := range slices.Sorted(maps.Keys(scripts)) {
		if strings.HasPrefix(script, behaviorsPrefix) && !slices.Contains(man.Assets, script) {
			man.Assets = append(man.Assets, script) // shipped under its own name
		}
	}
	var modules []string
	man.Assets = slices.DeleteFunc(man.Assets, func(a string) bool {
		if strings.HasSuffix(a, ".ts") && !scripts[a] {
			modules = append(modules, a)
			return true
		}
		return false
	})

	// build/ is CLI-owned and starts empty so stale artifacts never ship. The new
	// build goes to a staging folder that replaces build/ only once it's all there,
	// so a failed build (a script that doesn't compile) leaves the last good one
	// in place for the editor and the game.
	if err := os.RemoveAll(outDir); err != nil {
		return fmt.Errorf("Failed to clean the staging directory: %w", err)
	}

	if err := editManifest(archive.ManifestEntryKey, filepath.Join(outDir, archive.ManifestEntryKey),
		func(raw map[string]any) { raw["assets"] = append([]string{}, man.Assets...) }); err != nil { // [], never null: the editor reads an array
		return fmt.Errorf("Failed to write the built manifest: %w", err)
	}

	for _, asset := range man.Assets {
		source := asset
		if strings.HasPrefix(asset, behaviorsPrefix) {
			source = path.Join(scriptsPkgDir, "node_modules", asset)
			if !fileExists(source) {
				r.add(Diagnostic{Level: "error", Category: "script", File: asset, Message: "no such stock behavior (see docs: runtime-gameplay's Stock behaviors)"})
				continue
			}
		}
		if err := copyFile(source, filepath.Join(outDir, asset)); err != nil {
			return fmt.Errorf("Failed to copy %s: %w", asset, err)
		}
		say("Copied asset: %s", asset)
		if strings.HasSuffix(asset, ".ts") {
			stage("Building script %s", path.Base(asset))
			// Every script, then the content, before failing: one build
			// reports all the problems it can.
			if err := runAsc(r, tc, source, projectRoot, filepath.Join(projectRoot, outDir, asset)); err != nil {
				r.add(Diagnostic{Level: "error", Category: "script", File: asset, Message: "doesn't compile (asc: " + err.Error() + ")"})
				continue
			}
			say("Built script: %s", asset)
		}
	}
	checked := filepath.Join(projectRoot, outDir+".modules")
	for _, module := range modules {
		if err := runAsc(r, tc, module, projectRoot, filepath.Join(checked, module)); err != nil {
			r.add(Diagnostic{Level: "error", Category: "script", File: module, Message: "doesn't compile (asc: " + err.Error() + ")"})
			continue
		}
		say("Checked module: %s (no scene or prefab attaches it)", module)
	}
	if err := os.RemoveAll(checked); err != nil {
		return fmt.Errorf("Failed to clean up checked modules: %w", err)
	}
	for _, asset := range man.Assets {
		if strings.HasSuffix(asset, ".atlas.json") {
			if err := bakeAtlas(asset); err != nil {
				return Diagnostic{Category: "atlas", File: asset, Message: err.Error()}
			}
		}
		if strings.HasSuffix(asset, ".tsj") {
			if err := bakeTileset(asset); err != nil {
				return Diagnostic{Category: "tileset", File: asset, Message: err.Error()}
			}
		}
	}
	for _, scene := range man.Scenes {
		if err := copyFile(scene, filepath.Join(outDir, scene)); err != nil {
			return fmt.Errorf("Failed to copy %s: %w", scene, err)
		}
	}

	content := slices.Clone(man.Scenes)
	for _, f := range man.Assets {
		if strings.HasSuffix(f, ".prefab.json") {
			content = append(content, f)
		}
	}
	stage("Checking scenes")
	checkContent(r, slices.Compact(slices.Sorted(slices.Values(content))))
	if r.errors > 0 {
		return nil // r.end fails it: build/ keeps the last good build
	}

	if err := recordSources(outDir, sources); err != nil {
		return fmt.Errorf("Failed to record the build's sources: %w", err)
	}
	if err := swapBuild(); err != nil {
		return fmt.Errorf("Failed to replace build/: %w", err)
	}
	say("Build complete!")
	return nil
}
