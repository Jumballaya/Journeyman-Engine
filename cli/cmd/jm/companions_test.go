package main

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
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
		if did, err := setCompanion(name, true); err != nil || !strings.Contains(did, "restart") {
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
	if _, err := setCompanion("aseprite", false); err != nil {
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
	if _, err := setCompanion("basic-memory", true); err == nil || !strings.Contains(err.Error(), "uvx") {
		t.Errorf("without uvx: %v", err)
	}
	if _, err := setCompanion("photoshop", true); err == nil {
		t.Error("an unknown companion")
	}
	if did, err := setCompanion("tiled", true); err != nil || !strings.Contains(did, "jm setup") {
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
