package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

func TestInitWritesManifestWithDefaults(t *testing.T) {
	dir := t.TempDir()

	if err := runInit(dir, "my-game", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	man, err := manifest.LoadManifest(filepath.Join(dir, ".jm.json"))
	if err != nil {
		t.Fatalf("load manifest: %v", err)
	}
	if man.Name != "my-game" {
		t.Fatalf("name: got %q want my-game", man.Name)
	}
	if man.Version != "0.1.0" {
		t.Fatalf("version: got %q want 0.1.0", man.Version)
	}
	if man.EnginePath != "journeyman_engine" {
		t.Fatalf("engine: got %q", man.EnginePath)
	}
	if man.EntryScene != "scenes/main.scene.json" {
		t.Fatalf("entryScene: got %q", man.EntryScene)
	}
	if len(man.Scenes) != 1 || man.Scenes[0] != "scenes/main.scene.json" {
		t.Fatalf("scenes: got %v", man.Scenes)
	}
	if len(man.Assets) != 0 {
		t.Fatalf("assets: expected empty, got %v", man.Assets)
	}
}

func TestInitDefaultsNameToBasenameWhenOmitted(t *testing.T) {
	parent := t.TempDir()
	dir := filepath.Join(parent, "fancy-project")
	if err := os.Mkdir(dir, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}

	if err := runInit(dir, "", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	man, err := manifest.LoadManifest(filepath.Join(dir, ".jm.json"))
	if err != nil {
		t.Fatalf("load manifest: %v", err)
	}
	if man.Name != "fancy-project" {
		t.Fatalf("expected name from basename, got %q", man.Name)
	}
}

func TestInitWritesEntrySceneAsValidJson(t *testing.T) {
	dir := t.TempDir()
	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	data, err := os.ReadFile(filepath.Join(dir, "scenes/main.scene.json"))
	if err != nil {
		t.Fatalf("read entry scene: %v", err)
	}
	var parsed map[string]interface{}
	if err := json.Unmarshal(data, &parsed); err != nil {
		t.Fatalf("entry scene is not valid JSON: %v", err)
	}
	if _, ok := parsed["entities"]; !ok {
		t.Fatalf("entry scene missing entities field: %v", parsed)
	}
}

func TestInitRefusesWhenManifestExists(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, ".jm.json"), []byte("{}"), 0o644); err != nil {
		t.Fatalf("seed manifest: %v", err)
	}

	err := runInit(dir, "g", &bytes.Buffer{})
	if err == nil {
		t.Fatalf("expected error when .jm.json already exists")
	}
	if !strings.Contains(err.Error(), "already exists") {
		t.Fatalf("error should mention 'already exists', got: %v", err)
	}
}

func TestInitKeepsExistingEntryScene(t *testing.T) {
	dir := t.TempDir()
	scenePath := filepath.Join(dir, "scenes/main.scene.json")
	if err := os.MkdirAll(filepath.Dir(scenePath), 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	original := []byte(`{"name":"existing","entities":[{"name":"keep me"}]}`)
	if err := os.WriteFile(scenePath, original, 0o644); err != nil {
		t.Fatalf("seed scene: %v", err)
	}

	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	got, err := os.ReadFile(scenePath)
	if err != nil {
		t.Fatalf("read scene: %v", err)
	}
	if !bytes.Equal(got, original) {
		t.Fatalf("entry scene was clobbered: got %s", got)
	}
}

func TestInitAddsGitignoreEntries(t *testing.T) {
	dir := t.TempDir()
	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	data, err := os.ReadFile(filepath.Join(dir, ".gitignore"))
	if err != nil {
		t.Fatalf("read .gitignore: %v", err)
	}
	for _, line := range []string{"build/", "*.jm"} {
		if !strings.Contains(string(data), line) {
			t.Fatalf(".gitignore missing %q: %s", line, data)
		}
	}
}

func TestInitGeneratedProjectAcceptsGenerate(t *testing.T) {
	dir := t.TempDir()
	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	chdir(t, dir)
	g := findGenerator(t, "prefab")
	if err := runGenerate(g, "obstacle", &bytes.Buffer{}); err != nil {
		t.Fatalf("runGenerate after init: %v", err)
	}
	if _, err := os.Stat("assets/prefabs/obstacle.prefab.json"); err != nil {
		t.Fatalf("expected prefab created after init: %v", err)
	}
}
