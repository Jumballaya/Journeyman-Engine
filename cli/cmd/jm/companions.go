package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/plugin"

	"github.com/spf13/cobra"
)

// Companions are tools people make games with beside Journeyman. None is
// needed and jm installs none: the person turns on the ones they use, and
// Journeyman's plugin then brings each one's skill and MCP server.
type companion struct {
	name, title, use string
	app              []string // where the app may be: paths from the home folder ("~/…"), or absolute
	command          string   // its command on PATH, "" if none
	server           func(found string) *plugin.Server
	runner           string // the command its MCP server needs ("uv", "uvx"), "" if none
}

var companions = []companion{
	{name: "aseprite", title: "Aseprite", use: "draw and animate sprites; exports become atlases with clips",
		app: []string{"/Applications/Aseprite.app/Contents/MacOS/aseprite", "~/Library/Application Support/Steam/steamapps/common/Aseprite/Aseprite.app/Contents/MacOS/aseprite",
			`C:\Program Files\Aseprite\Aseprite.exe`, `C:\Program Files (x86)\Steam\steamapps\common\Aseprite\Aseprite.exe`},
		command: "aseprite", runner: "uv",
		server: func(found string) *plugin.Server { // github.com/diivi/aseprite-mcp, pinned
			return &plugin.Server{Command: "uv", Args: []string{"run", "--no-project", "--python", "3.13", "--with",
				"git+https://github.com/diivi/aseprite-mcp@90d1696a7e41edff89bbd0823ae6a5f86c114bcc", "--", "python", "-m", "aseprite_mcp"},
				Env: map[string]string{"ASEPRITE_PATH": found}}
		}},
	{name: "tiled", title: "Tiled", use: "paint tile maps; the game reads its .tmj and .tsj files",
		app:     []string{"/Applications/Tiled.app", `C:\Program Files\Tiled\tiled.exe`},
		command: "tiled"},
	{name: "obsidian", title: "Obsidian", use: "the game's design/ notes as a vault",
		app:     []string{"/Applications/Obsidian.app", "~/Applications/Obsidian.app", "~/AppData/Local/Programs/Obsidian/Obsidian.exe"},
		command: "obsidian"},
	{name: "basic-memory", title: "Basic Memory", use: "remembers the game's design and findings across chats, over design/",
		command: "basic-memory", runner: "uvx",
		server: func(string) *plugin.Server {
			return &plugin.Server{Command: "uvx", Args: []string{"--prerelease=allow", "basic-memory", "mcp"}}
		}},
}

// found is where c is on this machine, "" when it isn't.
func (c companion) found() string {
	for _, p := range c.app {
		if rest, ok := strings.CutPrefix(p, "~/"); ok {
			p = filepath.Join(homeDir(), filepath.FromSlash(rest))
		}
		if exists(p) {
			return p
		}
	}
	if c.command != "" {
		if path, err := exec.LookPath(c.command); err == nil {
			return path
		}
	}
	return ""
}

func findCompanion(name string) (companion, bool) {
	i := slices.IndexFunc(companions, func(c companion) bool { return c.name == name })
	if i < 0 {
		return companion{}, false
	}
	return companions[i], true
}

// companionsFile keeps the ones turned on: ~/.jm/companions.json.
func companionsFile() string { return filepath.Join(homeDir(), ".jm", "companions.json") }

func enabledCompanions() []string {
	var saved struct {
		Enabled []string `json:"enabled"`
	}
	data, _ := os.ReadFile(companionsFile())
	_ = json.Unmarshal(data, &saved)
	return slices.DeleteFunc(saved.Enabled, func(n string) bool { _, ok := findCompanion(n); return !ok })
}

// setCompanion turns name on or off and rewrites the installed plugin to
// match; it says what changed.
func setCompanion(name string, on bool) (string, error) {
	c, err := checkCompanion(name, on)
	if err != nil {
		return "", err
	}
	enabled := slices.DeleteFunc(enabledCompanions(), func(n string) bool { return n == name })
	if on {
		enabled = append(enabled, name)
	}
	data, _ := json.MarshalIndent(map[string]any{"enabled": enabled}, "", "  ")
	if err := os.MkdirAll(filepath.Dir(companionsFile()), 0o755); err != nil {
		return "", err
	}
	if err := atomicfile.WriteFile(companionsFile(), append(data, '\n'), 0o644); err != nil {
		return "", err
	}
	state := map[bool]string{true: "on", false: "off"}[on]
	if !exists(pluginSource()) {
		return fmt.Sprintf("%s is %s; jm setup codex (or chatgpt) installs the plugin that brings it.", c.title, state), nil
	}
	if err := refreshPlugin(); err != nil {
		return "", err
	}
	return fmt.Sprintf("%s is %s in Journeyman's plugin: restart the Codex or ChatGPT app.", c.title, state), nil
}

// checkCompanion is the companion name, if it can be turned on or off as asked.
func checkCompanion(name string, on bool) (companion, error) {
	c, ok := findCompanion(name)
	if !ok {
		return c, fmt.Errorf("no companion %q: there are %s", name, companionNames())
	}
	if on && c.runner != "" {
		if _, err := exec.LookPath(c.runner); err != nil {
			return c, fmt.Errorf("%s's MCP server runs with %s, which isn't installed (docs.astral.sh/uv): install it, then turn %s on", c.title, c.runner, name)
		}
	}
	return c, nil
}

// pluginOptions are what the plugin runs: jm (where it found it) and the companions turned on.
func pluginOptions(jm string) plugin.Options {
	o := plugin.Options{JM: jm, Version: version}
	for _, name := range enabledCompanions() {
		c, _ := findCompanion(name)
		pc := plugin.Companion{Name: c.name}
		if c.server != nil {
			pc.Server = c.server(c.found())
		}
		o.Companions = append(o.Companions, pc)
	}
	return o
}

// refreshPlugin writes the installed plugin again (its source and Codex's
// copy), for the jm it runs now.
func refreshPlugin() error {
	var installed struct {
		Servers map[string]struct {
			Command string `json:"command"`
		} `json:"mcpServers"`
	}
	data, _ := os.ReadFile(filepath.Join(pluginSource(), "mcp.json"))
	_ = json.Unmarshal(data, &installed)
	jm := installed.Servers[plugin.Name].Command
	if jm == "" {
		return fmt.Errorf("%s has no jm in it: run jm setup again", pluginSource())
	}
	cli := hasCommand("codex")()
	marketplace, err := installPlugin(jm, !cli)
	if err == nil && cli {
		if out, addErr := exec.Command("codex", "plugin", "add", pluginID(marketplace)).CombinedOutput(); addErr != nil {
			err = fmt.Errorf("codex plugin add: %s", strings.TrimSpace(string(out)))
		}
	}
	return err
}

func companionNames() string {
	var names []string
	for _, c := range companions {
		names = append(names, c.name)
	}
	return strings.Join(names, ", ")
}

// companionsText lists them: name, on or off, where found, what for.
func companionsText(w io.Writer) {
	enabled := enabledCompanions()
	for _, c := range companions {
		state := "off"
		if slices.Contains(enabled, c.name) {
			state = "on"
		}
		where := "not found here"
		if f := c.found(); f != "" {
			where = "found: " + f
		}
		fmt.Fprintf(w, "%-13s %-3s  %s (%s)\n", c.name, state, c.use, where)
	}
}

var companionsCmd = &cobra.Command{
	Use:   "companions",
	Short: "Tools beside Journeyman (Aseprite, Tiled, Obsidian, Basic Memory): turn them on for your agent",
	Long: `Companions are tools people make games with beside Journeyman. None is
needed, and jm installs none. Turning one on adds to Journeyman's agent
plugin (jm setup codex or chatgpt installs it) a skill that teaches the work
across it and Journeyman, and its MCP server where it has one:

  aseprite      draw and animate sprites (MCP: diivi/aseprite-mcp, run with uv)
  tiled         paint tile maps (a skill: the game reads Tiled's files)
  obsidian      the game's design/ notes as a vault (a skill: notes are files)
  basic-memory  remembers design and findings across chats (MCP: basic-memory)

  jm companions                  each one: on or off, and whether it's here
  jm companions enable <name>    turn one on
  jm companions disable <name>   turn it off`,
	Args: cobra.NoArgs,
	Run:  func(cmd *cobra.Command, _ []string) { companionsText(cmd.OutOrStdout()) },
}

func init() {
	for _, on := range []bool{true, false} {
		use := map[bool]string{true: "enable <name>", false: "disable <name>"}[on]
		companionsCmd.AddCommand(&cobra.Command{Use: use, Short: "Turn a companion " + map[bool]string{true: "on", false: "off"}[on],
			Args: cobra.ExactArgs(1), RunE: func(cmd *cobra.Command, args []string) error {
				did, err := setCompanion(args[0], on)
				if err == nil {
					fmt.Fprintln(cmd.OutOrStdout(), did)
				}
				return err
			}})
	}
}

func companionsTool() mcpTool {
	return mcpTool{Name: "companions", Title: "Companion tools",
		Description: "Tools beside Journeyman the person may use: Aseprite (sprites), Tiled (maps), Obsidian (design notes), Basic Memory (memory). " +
			"With no arguments: each one, on or off, and whether it's on this machine. enable/disable turns one on or off in Journeyman's plugin " +
			"(its skill and MCP server; the app needs a restart). Only when the person asks.",
		Annotations: writes(), InputSchema: object(map[string]any{
			"enable":  map[string]any{"type": "string", "description": "a companion to turn on"},
			"disable": map[string]any{"type": "string", "description": "a companion to turn off"}}),
		run: func(a toolArgs) toolResult {
			var b strings.Builder
			for name, on := range map[string]bool{a.str("enable"): true, a.str("disable"): false} {
				if name == "" {
					continue
				}
				did, err := setCompanion(name, on)
				if err != nil {
					return textResult(err.Error(), true)
				}
				b.WriteString(did + "\n")
			}
			companionsText(&b)
			return textResult(b.String(), false)
		}}
}
