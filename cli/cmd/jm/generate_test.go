package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// chdir cd's into dir for the duration of the test and restores cwd on cleanup.
// runGenerate writes relative to cwd, so each test gets an isolated workspace.
func chdir(t *testing.T, dir string) {
	t.Helper()
	prev, err := os.Getwd()
	if err != nil {
		t.Fatalf("getwd: %v", err)
	}
	if err := os.Chdir(dir); err != nil {
		t.Fatalf("chdir %s: %v", dir, err)
	}
	t.Cleanup(func() { os.Chdir(prev) })
}

// generateProject sets up an isolated temp dir with a stub .jm.json and chdir's
// into it. Most generate tests need this — the runGenerate guard refuses to
// write outside a Journeyman project root.
func generateProject(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	chdir(t, dir)
	if err := os.WriteFile(".jm.json", []byte("{}"), 0o644); err != nil {
		t.Fatalf("write stub manifest: %v", err)
	}
	return dir
}

func findGenerator(t *testing.T, kind string) generator {
	t.Helper()
	for _, g := range generators {
		if g.kind == kind {
			return g
		}
	}
	t.Fatalf("no generator registered for kind %q", kind)
	return generator{}
}

func TestGenerateScriptCreatesTsFile(t *testing.T) {
	generateProject(t)
	g := findGenerator(t, "script")

	var out bytes.Buffer
	if err := runGenerate(g, "weapon", &out); err != nil {
		t.Fatalf("runGenerate: %v", err)
	}

	data, err := os.ReadFile("assets/scripts/weapon.ts")
	if err != nil {
		t.Fatalf("expected file at assets/scripts/weapon.ts: %v", err)
	}
	if !strings.Contains(string(data), "onUpdate") {
		t.Fatalf("expected scaffold to include onUpdate, got: %s", data)
	}
}

func TestGeneratePrefabCreatesValidJson(t *testing.T) {
	generateProject(t)
	g := findGenerator(t, "prefab")

	if err := runGenerate(g, "obstacle", &bytes.Buffer{}); err != nil {
		t.Fatalf("runGenerate: %v", err)
	}

	data, err := os.ReadFile("assets/prefabs/obstacle.prefab.json")
	if err != nil {
		t.Fatalf("read generated prefab: %v", err)
	}
	var parsed map[string]interface{}
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("generated prefab is not valid JSON: %v", err)
	}
	if _, ok := parsed["components"]; !ok {
		t.Fatalf("generated prefab missing components field: %v", parsed)
	}
}

func TestGenerateSceneCreatesValidJson(t *testing.T) {
	generateProject(t)
	g := findGenerator(t, "scene")

	if err := runGenerate(g, "level3", &bytes.Buffer{}); err != nil {
		t.Fatalf("runGenerate: %v", err)
	}

	data, err := os.ReadFile("scenes/level3.scene.json")
	if err != nil {
		t.Fatalf("read generated scene: %v", err)
	}
	var parsed map[string]interface{}
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("generated scene is not valid JSON: %v", err)
	}
	if _, ok := parsed["entities"]; !ok {
		t.Fatalf("generated scene missing entities field: %v", parsed)
	}
}

func TestGenerateStripsTrailingSuffix(t *testing.T) {
	generateProject(t)
	g := findGenerator(t, "prefab")

	if err := runGenerate(g, "obstacle.prefab.json", &bytes.Buffer{}); err != nil {
		t.Fatalf("runGenerate: %v", err)
	}
	if _, err := os.Stat("assets/prefabs/obstacle.prefab.json"); err != nil {
		t.Fatalf("expected suffix-stripped path, got error: %v", err)
	}
	if _, err := os.Stat("assets/prefabs/obstacle.prefab.json.prefab.json"); !os.IsNotExist(err) {
		t.Fatalf("suffix was double-applied: %v", err)
	}
}

func TestGenerateAcceptsNestedName(t *testing.T) {
	generateProject(t)
	g := findGenerator(t, "prefab")

	if err := runGenerate(g, "weapons/sword", &bytes.Buffer{}); err != nil {
		t.Fatalf("runGenerate: %v", err)
	}
	if _, err := os.Stat(filepath.Join("assets/prefabs/weapons/sword.prefab.json")); err != nil {
		t.Fatalf("expected nested path created: %v", err)
	}
}

func TestGenerateRefusesToOverwrite(t *testing.T) {
	generateProject(t)
	g := findGenerator(t, "script")

	if err := runGenerate(g, "weapon", &bytes.Buffer{}); err != nil {
		t.Fatalf("first runGenerate: %v", err)
	}
	if err := runGenerate(g, "weapon", &bytes.Buffer{}); err == nil {
		t.Fatalf("expected second runGenerate to fail on existing file")
	}
}

func TestGenerateRejectsEscapingNames(t *testing.T) {
	generateProject(t)
	g := findGenerator(t, "script")

	for _, bad := range []string{"", "../escape", "/abs/path"} {
		if err := runGenerate(g, bad, &bytes.Buffer{}); err == nil {
			t.Fatalf("expected error for name %q", bad)
		}
	}
}

func TestGenerateRefusesWithoutManifest(t *testing.T) {
	chdir(t, t.TempDir()) // no .jm.json scaffolded
	g := findGenerator(t, "script")

	err := runGenerate(g, "weapon", &bytes.Buffer{})
	if err == nil {
		t.Fatalf("expected error when run outside a Journeyman project root")
	}
	if !strings.Contains(err.Error(), ".jm.json") {
		t.Fatalf("error should mention .jm.json, got: %v", err)
	}
	if _, statErr := os.Stat("assets/scripts/weapon.ts"); !os.IsNotExist(statErr) {
		t.Fatalf("guard should not have written any file: %v", statErr)
	}
}

func TestGenerateListEnumeratesAllKinds(t *testing.T) {
	var out bytes.Buffer
	if err := runGenerateList(&out); err != nil {
		t.Fatalf("runGenerateList: %v", err)
	}
	for _, g := range generators {
		if !strings.Contains(out.String(), g.kind) {
			t.Fatalf("list output missing kind %q: %s", g.kind, out.String())
		}
	}
}
