package main

import (
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"strings"
	"sync"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// onPath puts executables of these names on PATH (and nothing else).
func onPath(t *testing.T, names ...string) string {
	if runtime.GOOS == "windows" {
		t.Skip("shell-script commands")
	}
	bin := t.TempDir()
	for _, n := range names {
		os.WriteFile(filepath.Join(bin, n), []byte("#!/bin/sh\n"), 0o755)
	}
	t.Setenv("PATH", bin)
	return bin
}

// Turning companions on and off rewrites the installed plugin: their skills,
// and their MCP servers (the app found is passed to its server).
func TestCompanionsGoIntoThePlugin(t *testing.T) {
	pluginHome(t)
	if _, err := addToCodex("/x/jm"); err != nil {
		t.Fatal(err)
	}
	onPath(t, "uv", "aseprite") // the app's path, or (no Aseprite.app here) this one's
	for _, name := range []string{"tiled", "aseprite"} {
		if did, err := setCompanions(map[string]bool{name: true}); err != nil || !strings.Contains(did, "restart") {
			t.Fatalf("%s: %s %v", name, did, err)
		}
	}
	for _, dir := range []string{pluginSource(), pluginCache(personalMarketplace)} {
		mcp, _ := os.ReadFile(filepath.Join(dir, "mcp.json"))
		launcher, _ := os.ReadFile(filepath.Join(dir, "bin", "jm"))
		if !exists(filepath.Join(dir, "skills", "tiled", "SKILL.md")) || !strings.Contains(string(mcp), `"ASEPRITE_PATH": "/`) ||
			!strings.Contains(string(mcp), `"command": "./bin/jm"`) || !strings.Contains(string(launcher), "'/x/jm'") || strings.Contains(string(mcp), `"tiled"`) {
			t.Errorf("%s:\n%s", dir, mcp)
		}
	}
	if _, err := setCompanions(map[string]bool{"aseprite": false}); err != nil {
		t.Fatal(err)
	}
	if mcp, _ := os.ReadFile(filepath.Join(pluginSource(), "mcp.json")); strings.Contains(string(mcp), "aseprite") || exists(filepath.Join(pluginSource(), "skills", "aseprite")) {
		t.Errorf("still there after disable:\n%s", mcp)
	}
}

// A companion whose server needs uv isn't turned on without it; an unknown
// one is an error; with no plugin yet, the choice waits for jm setup.
func TestCompanionsSayWhatsMissing(t *testing.T) {
	pluginHome(t)
	onPath(t)
	if _, err := setCompanions(map[string]bool{"basic-memory": true}); err == nil || !strings.Contains(err.Error(), "uvx") {
		t.Errorf("without uvx: %v", err)
	}
	if _, err := setCompanions(map[string]bool{"photoshop": true}); err == nil {
		t.Error("an unknown companion")
	}
	if did, err := setCompanions(map[string]bool{"tiled": true}); err != nil || !strings.Contains(did, "jm setup") {
		t.Errorf("no plugin yet: %s %v", did, err)
	}
	if got := enabledCompanions(); len(got) != 1 || got[0] != "tiled" {
		t.Errorf("enabled: %v", got)
	}
}

// ChatGPT's settings page turns them on and off too.
func TestCompanionsAreSettings(t *testing.T) {
	pluginHome(t)
	onPath(t)
	inProject(t, map[string]string{})
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"companions.tiled": true}}); got.Failed {
		t.Fatal(got.Text)
	} else if got.Structured.(map[string]any)["values"].(map[string]any)["companions.tiled"] != true {
		t.Errorf("values: %v", got.Structured)
	}
	before, _ := os.ReadFile(projects.PreferencesFile())
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"companions.basic-memory": true, "editor.appearance": "dark"}}); !got.Failed {
		t.Error("basic-memory without uvx was turned on")
	}
	if after, _ := os.ReadFile(projects.PreferencesFile()); string(after) != string(before) {
		t.Error("a failed update changed the preferences")
	}
}

// Companions turned on at once (other jm processes take the same lock) all
// land, in the choices and in the installed plugin, with or without one.
func TestCompanionsTurnedOnAtOnceAllLand(t *testing.T) {
	for _, installed := range []bool{false, true} {
		for range 10 {
			pluginHome(t)
			if installed {
				if _, err := addToCodex("/x/jm"); err != nil {
					t.Fatal(err)
				}
			}
			var wg sync.WaitGroup
			for _, name := range []string{"tiled", "obsidian"} {
				wg.Add(1)
				go func() {
					defer wg.Done()
					if _, err := setCompanions(map[string]bool{name: true}); err != nil {
						t.Error(err)
					}
				}()
			}
			if installed { // setup refreshing the plugin meanwhile
				wg.Add(1)
				go func() { defer wg.Done(); addToCodex("/x/jm") }()
			}
			wg.Wait()
			if got := strings.Join(enabledCompanions(), ","); got != "tiled,obsidian" && got != "obsidian,tiled" {
				t.Fatalf("installed %v: saved %s", installed, got)
			}
			for _, dir := range map[bool][]string{false: nil, true: {pluginSource(), pluginCache(personalMarketplace)}}[installed] {
				for _, f := range []string{"skills/tiled/SKILL.md", "skills/obsidian/SKILL.md", "bin/jm", "mcp.json"} {
					if !exists(filepath.Join(dir, f)) {
						t.Fatalf("%s has no %s", dir, f)
					}
				}
			}
		}
	}
}

// When the installed plugin can't be rebuilt, the choices stay as they were
// with it: from the CLI, the companions tool and settings_update alike.
func TestAFailedPluginRefreshKeepsTheChoices(t *testing.T) {
	home := pluginHome(t)
	if _, err := addToCodex("/x/jm"); err != nil {
		t.Fatal(err)
	}
	if _, err := setCompanions(map[string]bool{"obsidian": true}); err != nil {
		t.Fatal(err)
	}
	failingCodex(t, "plugin", "add") // codex plugin add fails; the rest works
	before := tree(t, home)
	if _, err := setCompanions(map[string]bool{"tiled": true}); err == nil {
		t.Error("enable said it worked")
	}
	if got := companionsTool().run(toolArgs{"enable": "tiled"}); !got.Failed {
		t.Errorf("companions tool: %s", got.Text)
	}
	if got := settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"companions.tiled": true, "companions.obsidian": false}}); !got.Failed {
		t.Errorf("settings_update: %s", got.Text)
	}
	if after := tree(t, home); !reflect.DeepEqual(after, before) {
		t.Errorf("changed:\n%v\nwas\n%v", after, before)
	}
	fakeCLI(t, "codex", "")
	if _, err := setCompanions(map[string]bool{"tiled": true}); err != nil {
		t.Fatal(err)
	}
	if got := strings.Join(enabledCompanions(), ","); got != "obsidian,tiled" || !exists(filepath.Join(pluginSource(), "skills", "tiled", "SKILL.md")) || !exists(filepath.Join(pluginSource(), "skills", "obsidian", "SKILL.md")) {
		t.Errorf("after the failure's gone: %s", got)
	}
}

// Aseprite's server runs the Aseprite ASEPRITE_PATH names, else the one found
// on PATH; with none, it's given no empty one.
func TestAsepriteIsTheOneTheVariableNames(t *testing.T) {
	pluginHome(t)
	if _, err := addToCodex("/x/jm"); err != nil {
		t.Fatal(err)
	}
	bin := onPath(t, "uv")
	custom := filepath.Join(t.TempDir(), "my-aseprite")
	os.WriteFile(custom, []byte("#!/bin/sh\n"), 0o755)
	server := func() string {
		data, _ := os.ReadFile(filepath.Join(pluginSource(), "mcp.json"))
		return string(data)
	}
	t.Setenv("ASEPRITE_PATH", custom)
	setCompanions(map[string]bool{"aseprite": true})
	if !strings.Contains(server(), `"ASEPRITE_PATH": "`+custom+`"`) {
		t.Errorf("the variable's: %s", server())
	}
	t.Setenv("ASEPRITE_PATH", "")
	if c, _ := findCompanion("aseprite"); c.found() != "" {
		t.Skip("Aseprite is installed here, and wins over PATH")
	}
	os.WriteFile(filepath.Join(bin, "aseprite"), []byte("#!/bin/sh\n"), 0o755)
	setCompanions(map[string]bool{"aseprite": true})
	if !strings.Contains(server(), `"ASEPRITE_PATH": "`+filepath.Join(bin, "aseprite")+`"`) {
		t.Errorf("PATH's: %s", server())
	}
	os.Remove(filepath.Join(bin, "aseprite"))
	setCompanions(map[string]bool{"aseprite": true})
	if strings.Contains(server(), "ASEPRITE_PATH") || !strings.Contains(server(), `"aseprite"`) {
		t.Errorf("none found: %s", server())
	}
}
