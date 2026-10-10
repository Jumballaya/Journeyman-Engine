package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"image/png"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
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
	Run: func(cmd *cobra.Command, args []string) {
		say("Building game...")

		projectRoot, err := os.Getwd()
		exitOnError("Failed to resolve project root", err)

		// Toolchain errors are already actionable; print them as-is.
		tc, err := scriptToolchain(projectRoot)
		if err != nil {
			fail(Diagnostic{Category: "toolchain", Message: err.Error()})
		}

		man, err := manifest.LoadManifest(archive.ManifestEntryKey)
		exitOnError("Error loading manifest", err)

		// npm install prunes @jm/runtime and libraries (they aren't in package.json), so re-extract them.
		exitOnError("Failed to sync script packages", syncScriptPackages(projectRoot, man, tc))

		man.Assets, err = manifest.ExpandAssets(os.DirFS(projectRoot), man.Assets)
		exitOnError("Failed to expand asset patterns", err)

		// Validate every path before touching the filesystem.
		for _, p := range slices.Concat(man.Assets, man.Scenes) {
			exitOnError(fmt.Sprintf("Invalid manifest path %q", p), validateRelativePath(p))
			if strings.HasSuffix(p, ".script.json") {
				fail(Diagnostic{Category: "manifest", File: p, Message: "a legacy .script.json asset; run `jm migrate` to convert the project"})
			}
		}

		// A .ts asset is a script when content attaches it (a ScriptComponent in
		// a scene, prefab, map or data file names it, or the manifest does). The rest are modules that
		// scripts import (or scripts not attached yet): compiled, so their errors
		// show, but not shipped on their own.
		for _, d := range slices.Concat(scriptNameProblems(man), inputActionProblems(man)) {
			emit(d)
		}
		// The manifest too: a dedicated server's scripts are in net.server.scripts.
		scripts := referencedScripts(slices.Concat([]string{archive.ManifestEntryKey}, man.Scenes, man.Assets))
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
		exitOnError("Failed to clean the staging directory", os.RemoveAll(outDir))

		exitOnError("Failed to write the built manifest",
			editManifest(archive.ManifestEntryKey, filepath.Join(outDir, archive.ManifestEntryKey),
				func(raw map[string]any) { raw["assets"] = man.Assets }))

		for _, asset := range man.Assets {
			exitOnError("Failed to copy "+asset, copyFile(asset, filepath.Join(outDir, asset)))
			say("Copied asset: %s", asset)
			if strings.HasSuffix(asset, ".ts") {
				// Every script, then the content, before failing: one build
				// reports all the problems it can.
				if err := runAsc(tc, asset, projectRoot, filepath.Join(projectRoot, outDir, asset)); err != nil {
					emit(Diagnostic{Level: "error", Category: "script", File: asset, Message: "doesn't compile (asc: " + err.Error() + ")"})
					continue
				}
				say("Built script: %s", asset)
			}
		}
		checked := filepath.Join(projectRoot, outDir+".modules")
		for _, module := range modules {
			if err := runAsc(tc, module, projectRoot, filepath.Join(checked, module)); err != nil {
				emit(Diagnostic{Level: "error", Category: "script", File: module, Message: "doesn't compile (asc: " + err.Error() + ")"})
				continue
			}
			say("Checked module: %s (no scene or prefab attaches it)", module)
		}
		exitOnError("Failed to clean up checked modules", os.RemoveAll(checked))
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
		checkContent(slices.Compact(slices.Sorted(slices.Values(content))))
		if errorCount > 0 {
			finish(false) // build/ keeps the last good build
		}

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
	tc.LinkInto(scriptsPath(projectRoot))
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

// runAsc compiles a script (project-relative path) to outFile, through a
// generated entry under node_modules/.jm so @jm/runtime resolves normally.
func runAsc(tc toolchain.Toolchain, scriptPath, projectRoot, outFile string) error {
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
		emit(d)
	}
	return err
}

// Literal names scripts pass to spawn() and Scene.load(), which the engine
// resolves at run time ("brick" is assets/prefabs/brick.prefab.json); a name
// built at run time ("pickup_" + kind) isn't one.
var scriptNameUses = []struct {
	call   *regexp.Regexp
	suffix string
	what   string
}{
	{regexp.MustCompile(`\bspawn\(\s*"([^"]+)"\s*[,)]`), ".prefab.json", "prefab"},
	{regexp.MustCompile(`\bScene\.load\(\s*"([^"]+)"\s*[,)]`), ".scene.json", "scene"},
}

// scriptSource is a script's text with its comments blanked out (same length,
// so offsets still give lines and columns): what checks of its calls read.
type scriptSource struct{ file, text string }

func scriptSources(man manifest.GameManifest) []scriptSource {
	var out []scriptSource
	for _, file := range man.Assets {
		if !strings.HasSuffix(file, ".ts") {
			continue
		}
		if data, err := os.ReadFile(file); err == nil {
			out = append(out, scriptSource{file, blankComments(string(data))})
		}
	}
	return out
}

// blankComments turns // and /* */ comments outside string literals into
// spaces, keeping newlines.
func blankComments(text string) string {
	b := []byte(text)
	var quote byte // the open string's quote, or 0
	for i := 0; i < len(b); i++ {
		switch {
		case quote != 0:
			if b[i] == '\\' {
				i++
			} else if b[i] == quote {
				quote = 0
			}
		case b[i] == '"' || b[i] == '\'' || b[i] == '`':
			quote = b[i]
		case b[i] == '/' && i+1 < len(b) && (b[i+1] == '/' || b[i+1] == '*'):
			block := b[i+1] == '*'
			for ; i < len(b); i++ {
				if !block && b[i] == '\n' {
					break
				}
				if block && b[i] == '*' && i+1 < len(b) && b[i+1] == '/' {
					b[i], b[i+1] = ' ', ' '
					i++
					break
				}
				if b[i] != '\n' {
					b[i] = ' '
				}
			}
		}
	}
	return string(b)
}

// at gives a diagnostic's line and column (1-based) for an offset in s.text.
func (s scriptSource) at(offset int) (int, int) {
	line := strings.Count(s.text[:offset], "\n") + 1
	return line, offset - strings.LastIndex(s.text[:offset], "\n")
}

// scriptNameProblems are warnings for literal prefab and scene names in
// scripts that no listed file answers to: a typo found at build, not at spawn.
func scriptNameProblems(man manifest.GameManifest) []Diagnostic {
	var problems []Diagnostic
	listed := slices.Concat(man.Scenes, man.Assets)
	for _, src := range scriptSources(man) {
		for _, use := range scriptNameUses {
			for _, m := range use.call.FindAllStringSubmatchIndex(src.text, -1) {
				name := src.text[m[2]:m[3]]
				var names []string
				found := false
				for _, p := range listed {
					if !strings.HasSuffix(p, use.suffix) {
						continue
					}
					short := strings.TrimSuffix(path.Base(p), use.suffix)
					names = append(names, short)
					found = found || p == name || short == name
				}
				if !found {
					line, col := src.at(m[2])
					problems = append(problems, Diagnostic{Level: "warning", Category: "script", File: src.file, Line: line, Column: col,
						Message: fmt.Sprintf("no %s named %q in .jm.json%s", use.what, name, schema.Suggest(name, names))})
				}
			}
		}
	}
	return problems
}

// inputCall finds an Input call; actionArgs is how many of its leading string
// arguments name actions (bind's first defines one).
var (
	inputCall  = regexp.MustCompile(`\bInput\.(down|pressed|released|value|repeated|axis|vector|bind)\(`)
	stringArg  = regexp.MustCompile(`^\s*"([^"]*)"\s*([,)]?)`)
	actionArgs = map[string]int{"down": 1, "pressed": 1, "released": 1, "value": 1, "repeated": 1, "axis": 2, "vector": 4, "bind": 1}
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
		for _, m := range inputCall.FindAllStringSubmatchIndex(src.text, -1) {
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
// data) and every quoted .ts path in the scripts among them.
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
				for _, m := range scriptPathLiteral.FindAllStringSubmatch(string(data), -1) {
					walk(m[1])
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

func exitOnError(msg string, err error) {
	if err != nil {
		fail(Diagnostic{Category: "build", Message: fmt.Sprintf("%s: %s", msg, err)})
	}
}

// buildIsStale says whether the project's build is missing or older than any
// of its sources (the manifest, scenes, assets).
func buildIsStale(root string) bool {
	built, err := os.Stat(filepath.Join(root, "build", archive.ManifestEntryKey))
	if err != nil {
		return true
	}
	stale := false
	for _, top := range []string{archive.ManifestEntryKey, "scenes", "assets"} {
		_ = filepath.WalkDir(filepath.Join(root, top), func(path string, d fs.DirEntry, err error) error {
			if err != nil || stale {
				return filepath.SkipAll
			}
			if d.IsDir() && (d.Name() == "node_modules" || (strings.HasPrefix(d.Name(), ".") && path != filepath.Join(root, top))) {
				return filepath.SkipDir
			}
			if info, err := d.Info(); err == nil && !d.IsDir() && info.ModTime().After(built.ModTime()) {
				stale = true
			}
			return nil
		})
	}
	return stale
}
