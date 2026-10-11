package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"slices"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

// sceneTemplate is a new scene; jm init writes one too.
const sceneTemplate = `{
  "name": "",
  "entities": []
}
`

// generator is one kind of file `jm generate` scaffolds; each becomes a
// subcommand and a line of `jm generate list`.
type generator struct {
	kind    string
	dir     string // output directory relative to the project root
	suffix  string
	summary string
	body    string // file contents; a scene's empty "name" gets the file's name
}

var generators = []generator{
	{
		kind:    "script",
		dir:     "assets/scripts",
		suffix:  ".ts",
		summary: "AssemblyScript script with onUpdate and onOverlapStart",
		body: `// Runs on an entity with a ScriptComponent. Top-level code runs once when
// the entity starts; module variables are this entity's state.
// API reference: jm docs scripting (sources: node_modules/@jm/runtime/).
import { Entity, Input, self } from "@jm/runtime";

const me = self();
const SPEED: f32 = 200;

// Called every frame; dt is in seconds.
export function onUpdate(dt: f32): void {
  me.transform.x += Input.axis("left", "right") * SPEED * dt;
}

// Called when this entity's collider starts touching another (optional).
// Also: onOverlap (every frame), onOverlapEnd, onLanded, onLeftGround, onDestroy.
export function onOverlapStart(other: Entity): void {
}
`,
	},
	{
		kind:    "prefab",
		dir:     "assets/prefabs",
		suffix:  ".prefab.json",
		summary: "Empty prefab with no components or tags",
		body: `{
  "components": {},
  "tags": []
}
`,
	},
	{
		kind:    "ui",
		dir:     "assets/ui",
		suffix:  ".ui.html",
		summary: "HTML/CSS UI screen (attach with a UIDocumentComponent)",
		body: `<style>
  /* Lengths are logical pixels (config.renderer.logicalWidth/Height). */
  #root { width: 100%; height: 100%; display: flex; flex-direction: column;
          align-items: center; justify-content: center; gap: 8px; }
  h1 { font-size: 24px; color: white; }
  .hint { font-size: 12px; color: #aab; }
</style>
<div id="root">
  <h1 id="title">New Screen</h1>
  <p class="hint">Change me from a script: UI.setText("title", "Hello")</p>
</div>
`,
	},
	{
		kind:    "shader",
		dir:     "assets/shaders",
		suffix:  ".frag",
		summary: "Post-effect / transition fragment shader (PostEffect.custom)",
		body: `// Inputs (declared for you): u_primary (frame so far / incoming scene),
// u_aux (outgoing scene, transitions), u_progress (0 -> 1, transitions),
// u_resolution, u_viewport (x, y, w, h of the game area), u_logical, u_time,
// v_texCoord. Write the result to outColor.
void main() {
  outColor = texture(u_primary, v_texCoord);
}
`,
	},
	{
		kind:    "bindings",
		dir:     "assets",
		suffix:  ".bindings.json",
		summary: "Input action bindings (keys + gamepad) for Input.down/justPressed",
		body:    bindingsTemplate,
	},
	{
		kind:    "scene",
		dir:     "scenes",
		suffix:  ".scene.json",
		summary: "Empty scene with no entities",
		body:    sceneTemplate,
	},
}

var generateCmd = &cobra.Command{
	Use:   "generate",
	Short: "Scaffold a new script, prefab, scene, ui screen, shader, or bindings file",
	Long:  "Create empty source files for the project. Run `jm generate list` to see what can be generated.",
}

func init() {
	generateCmd.AddCommand(generateListCmd)
	for _, g := range generators {
		generateCmd.AddCommand(newGenerateSubcommand(g))
	}
}

var generateListCmd = &cobra.Command{
	Use:   "list",
	Short: "List the kinds of files `jm generate` can create",
	RunE: func(cmd *cobra.Command, args []string) error {
		return runGenerateList(cmd.OutOrStdout())
	},
}

func runGenerateList(out io.Writer) error {
	sorted := append([]generator(nil), generators...)
	sort.Slice(sorted, func(i, j int) bool { return sorted[i].kind < sorted[j].kind })
	for _, g := range sorted {
		fmt.Fprintf(out, "%-8s  %s/<name>%s  — %s\n", g.kind, g.dir, g.suffix, g.summary)
	}
	return nil
}

func newGenerateSubcommand(g generator) *cobra.Command {
	return &cobra.Command{
		Use:   g.kind + " <name>",
		Short: "Generate a new " + g.kind,
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return runGenerate(g, args[0], cmd.OutOrStdout())
		},
	}
}

func runGenerate(g generator, rawName string, out io.Writer) error {
	if _, err := os.Stat(archive.ManifestEntryKey); err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("generate %s: no %s in current directory — run from a Journeyman project root", g.kind, archive.ManifestEntryKey)
		}
		return fmt.Errorf("generate %s: stat %s: %w", g.kind, archive.ManifestEntryKey, err)
	}
	name := strings.TrimSpace(rawName)
	if name == "" {
		return fmt.Errorf("generate %s: name is required", g.kind)
	}
	// `weapon` and `weapons/sword.prefab.json` both work: strip a typed suffix.
	name = strings.TrimSuffix(name, g.suffix)
	cleaned := filepath.Clean(filepath.FromSlash(name))
	if cleaned == "." || strings.HasPrefix(cleaned, "..") || filepath.IsAbs(cleaned) {
		return fmt.Errorf("generate %s: invalid name %q", g.kind, rawName)
	}

	relPath := filepath.Join(g.dir, cleaned+g.suffix)
	created, err := writeIfMissing(relPath, []byte(bodyNamed(g.body, filepath.Base(cleaned))))
	if err != nil {
		return fmt.Errorf("generate %s: write %s: %w", g.kind, relPath, err)
	}
	if !created {
		return fmt.Errorf("generate %s: %s already exists", g.kind, relPath)
	}
	fmt.Fprintf(out, "Created %s\n", relPath)

	field := "assets"
	if g.kind == "scene" {
		field = "scenes"
	}
	added, err := addToManifestArray(archive.ManifestEntryKey, field, filepath.ToSlash(relPath))
	if err != nil {
		return fmt.Errorf("generate %s: register in %s: %w", g.kind, archive.ManifestEntryKey, err)
	}
	if added {
		fmt.Fprintf(out, "Registered in %s (%s[])\n", archive.ManifestEntryKey, field)
	} else {
		fmt.Fprintf(out, "Already listed in %s (%s[])\n", archive.ManifestEntryKey, field)
	}
	if g.kind == "script" {
		fmt.Fprintf(out, "Attach it to run it: \"ScriptComponent\": { \"script\": %q } in a scene or prefab\n", filepath.ToSlash(relPath))
	}
	return nil
}

// addToManifestArray adds value to the manifest's sorted `field` list unless
// it is already listed there (for assets, by a pattern too). Returns whether
// it was added.
func addToManifestArray(manifestPath, field, value string) (bool, error) {
	man, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return false, err
	}
	listed := man.Scenes
	if field == "assets" {
		if listed, err = manifest.ExpandAssets(os.DirFS(filepath.Dir(manifestPath)), man.Assets); err != nil {
			return false, err
		}
	}
	if slices.Contains(listed, value) {
		return false, nil
	}
	return true, editManifest(manifestPath, manifestPath, func(raw map[string]any) {
		existing, _ := raw[field].([]any)
		list := []string{value}
		for _, e := range existing {
			if s, ok := e.(string); ok {
				list = append(list, s)
			}
		}
		sort.Strings(list)
		raw[field] = list
	})
}

// bodyNamed fills a template's empty "name" (a scene's) with the file's name.
func bodyNamed(body, name string) string {
	quoted, _ := json.Marshal(name)
	return strings.Replace(body, `"name": ""`, `"name": `+string(quoted), 1)
}

// bindingsTemplate: jm generate bindings, and the input.bindings.json jm init
// writes (the script template reads its left/right actions).
const bindingsTemplate = `{
  "actions": {
    "left":    ["ArrowLeft", "A", "Gamepad.DPadLeft", "Gamepad.LeftStickLeft"],
    "right":   ["ArrowRight", "D", "Gamepad.DPadRight", "Gamepad.LeftStickRight"],
    "up":      ["ArrowUp", "W", "Gamepad.DPadUp", "Gamepad.LeftStickUp"],
    "down":    ["ArrowDown", "S", "Gamepad.DPadDown", "Gamepad.LeftStickDown"],
    "jump":    ["Space", "Z", "Gamepad.A"],
    "confirm": ["Enter", "Space", "Gamepad.A"],
    "back":    ["Escape", "Backspace", "Gamepad.B"],
    "pause":   ["Escape", "P", "Gamepad.Start"]
  }
}
`
