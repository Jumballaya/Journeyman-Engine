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

func TestInitScaffoldsScriptsPackage(t *testing.T) {
	dir := t.TempDir()
	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	scripts := filepath.Join(dir, "assets", "scripts")
	for _, name := range []string{"package.json", "asconfig.json", "tsconfig.json", ".gitignore"} {
		path := filepath.Join(scripts, name)
		data, err := os.ReadFile(path)
		if err != nil {
			t.Fatalf("expected %s to exist: %v", path, err)
		}
		if len(data) == 0 {
			t.Fatalf("%s is empty", path)
		}
	}

	pkgData, err := os.ReadFile(filepath.Join(scripts, "package.json"))
	if err != nil {
		t.Fatalf("read package.json: %v", err)
	}
	if !strings.Contains(string(pkgData), "assemblyscript") {
		t.Fatalf("package.json missing assemblyscript dep: %s", pkgData)
	}

	gitignore, err := os.ReadFile(filepath.Join(scripts, ".gitignore"))
	if err != nil {
		t.Fatalf("read .gitignore: %v", err)
	}
	if !strings.Contains(string(gitignore), "node_modules/") {
		t.Fatalf("scripts .gitignore missing node_modules/: %s", gitignore)
	}
}

func TestInitKeepsExistingScriptsPackageJson(t *testing.T) {
	dir := t.TempDir()
	scripts := filepath.Join(dir, "assets", "scripts")
	if err := os.MkdirAll(scripts, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	custom := []byte(`{"name":"my-custom","private":true}`)
	pkgPath := filepath.Join(scripts, "package.json")
	if err := os.WriteFile(pkgPath, custom, 0o644); err != nil {
		t.Fatalf("seed package.json: %v", err)
	}

	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	got, err := os.ReadFile(pkgPath)
	if err != nil {
		t.Fatalf("read package.json: %v", err)
	}
	if !bytes.Equal(got, custom) {
		t.Fatalf("custom package.json was clobbered: got %s", got)
	}
}

func TestInitKeepsExistingScriptsAsconfig(t *testing.T) {
	dir := t.TempDir()
	scripts := filepath.Join(dir, "assets", "scripts")
	if err := os.MkdirAll(scripts, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	custom := []byte(`{"options":{"exportRuntime":true,"optimizeLevel":3}}`)
	path := filepath.Join(scripts, "asconfig.json")
	if err := os.WriteFile(path, custom, 0o644); err != nil {
		t.Fatalf("seed asconfig.json: %v", err)
	}

	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	got, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read asconfig.json: %v", err)
	}
	if !bytes.Equal(got, custom) {
		t.Fatalf("custom asconfig.json was clobbered: got %s", got)
	}
}

func TestInitKeepsExistingScriptsTsconfig(t *testing.T) {
	dir := t.TempDir()
	scripts := filepath.Join(dir, "assets", "scripts")
	if err := os.MkdirAll(scripts, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	custom := []byte(`{"extends":"./custom.json","include":["src/**/*.ts"]}`)
	path := filepath.Join(scripts, "tsconfig.json")
	if err := os.WriteFile(path, custom, 0o644); err != nil {
		t.Fatalf("seed tsconfig.json: %v", err)
	}

	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	got, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read tsconfig.json: %v", err)
	}
	if !bytes.Equal(got, custom) {
		t.Fatalf("custom tsconfig.json was clobbered: got %s", got)
	}
}

func TestInitKeepsExistingScriptsGitignore(t *testing.T) {
	dir := t.TempDir()
	scripts := filepath.Join(dir, "assets", "scripts")
	if err := os.MkdirAll(scripts, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}
	custom := []byte("# my custom ignores\nfoo/\nbar.txt\n")
	path := filepath.Join(scripts, ".gitignore")
	if err := os.WriteFile(path, custom, 0o644); err != nil {
		t.Fatalf("seed .gitignore: %v", err)
	}

	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	got, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read .gitignore: %v", err)
	}
	if !bytes.Equal(got, custom) {
		t.Fatalf("custom .gitignore was clobbered: got %s", got)
	}
}

func TestInitGeneratedConfigsAreValidJSON(t *testing.T) {
	dir := t.TempDir()
	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	scripts := filepath.Join(dir, "assets", "scripts")
	for _, name := range []string{"package.json", "asconfig.json", "tsconfig.json"} {
		path := filepath.Join(scripts, name)
		data, err := os.ReadFile(path)
		if err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		var parsed map[string]interface{}
		if err := json.Unmarshal(data, &parsed); err != nil {
			t.Fatalf("%s is not valid JSON: %v\ncontents: %s", name, err, data)
		}
	}
}

func TestInitScriptsGitignoreContent(t *testing.T) {
	dir := t.TempDir()
	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	data, err := os.ReadFile(filepath.Join(dir, "assets", "scripts", ".gitignore"))
	if err != nil {
		t.Fatalf("read scripts .gitignore: %v", err)
	}
	for _, line := range []string{"node_modules/", "*.wasm"} {
		if !strings.Contains(string(data), line) {
			t.Fatalf("scripts .gitignore missing %q: %s", line, data)
		}
	}
}

func TestInitDoesNotTouchExistingNodeModules(t *testing.T) {
	dir := t.TempDir()
	nodeModules := filepath.Join(dir, "assets", "scripts", "node_modules")
	if err := os.MkdirAll(nodeModules, 0o755); err != nil {
		t.Fatalf("mkdir node_modules: %v", err)
	}
	seeded := []byte("preserved contents\n")
	seededPath := filepath.Join(nodeModules, "somefile")
	if err := os.WriteFile(seededPath, seeded, 0o644); err != nil {
		t.Fatalf("seed node_modules file: %v", err)
	}

	if err := runInit(dir, "g", &bytes.Buffer{}); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	got, err := os.ReadFile(seededPath)
	if err != nil {
		t.Fatalf("read seeded file: %v", err)
	}
	if !bytes.Equal(got, seeded) {
		t.Fatalf("node_modules file was modified: got %s", got)
	}
}

func TestInitNextStepsMessageIsClean(t *testing.T) {
	dir := t.TempDir()
	chdir(t, dir)

	var buf bytes.Buffer
	if err := runInit(".", "g", &buf); err != nil {
		t.Fatalf("runInit: %v", err)
	}

	out := buf.String()
	if !strings.Contains(out, "cd assets/scripts && npm install") {
		t.Fatalf("expected clean cd hint in output, got: %s", out)
	}
	if strings.Contains(out, "cd ./assets/scripts") {
		t.Fatalf("output should not contain './' prefix in cd hint: %s", out)
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
