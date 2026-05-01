package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// scaffold writes a minimal Journeyman project under `dir`. Returns nothing —
// each test inspects the directory afterwards.
//
// Project shape:
//   .jm.json with assets[] = ["assets/scripts/player.script.json"]
//   assets/scripts/player.ts        (TypeScript source — empty body OK)
//   assets/scripts/player.script.json (legacy manifest pointing at .ts/.wasm)
//   scenes/level1.scene.json        (references the .script.json)
func scaffold(t *testing.T, dir string) {
	t.Helper()

	man := manifest.GameManifest{
		Name:       "Test Game",
		Version:    "0.0.1",
		EnginePath: "/usr/bin/true",
		EntryScene: "scenes/level1.scene.json",
		Scenes:     []string{"scenes/level1.scene.json"},
		Assets:     []string{"assets/scripts/player.script.json"},
	}
	manData, _ := json.MarshalIndent(man, "", "  ")
	mustWrite(t, filepath.Join(dir, ".jm.json"), manData)

	mustWrite(t, filepath.Join(dir, "assets/scripts/player.ts"), []byte("// player\n"))

	sa := manifest.ScriptAsset{
		Name:    "player",
		Script:  "assets/scripts/player.ts",
		Binary:  "assets/scripts/player.wasm",
		Imports: []string{},
		Exposed: []string{},
	}
	saData, _ := json.MarshalIndent(sa, "", "  ")
	mustWrite(t, filepath.Join(dir, "assets/scripts/player.script.json"), saData)

	scene := map[string]interface{}{
		"name": "Level 1",
		"entities": []interface{}{
			map[string]interface{}{
				"name": "Player",
				"components": map[string]interface{}{
					"ScriptComponent": map[string]interface{}{
						"script": "assets/scripts/player.script.json",
					},
				},
			},
		},
	}
	sceneData, _ := json.MarshalIndent(scene, "", "  ")
	mustWrite(t, filepath.Join(dir, "scenes/level1.scene.json"), sceneData)
}

func mustWrite(t *testing.T, path string, data []byte) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatalf("mkdir %s: %v", path, err)
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

// initRepo runs `git init` in dir + makes an initial commit so the working
// tree is clean by default. Tests that need a dirty tree dirty it themselves.
func initRepo(t *testing.T, dir string) {
	t.Helper()
	for _, args := range [][]string{
		{"init", "-q"},
		{"-c", "user.email=t@t", "-c", "user.name=t", "add", "-A"},
		{"-c", "user.email=t@t", "-c", "user.name=t", "commit", "-q", "-m", "init"},
	} {
		cmd := exec.Command("git", args...)
		cmd.Dir = dir
		if out, err := cmd.CombinedOutput(); err != nil {
			t.Skipf("git not usable in this env (%v: %s)", err, out)
		}
	}
}

func TestMigrateRewritesSceneScriptReferences(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	initRepo(t, dir)

	var stdout bytes.Buffer
	if err := runMigrate(dir, &stdout, false, false); err != nil {
		t.Fatalf("runMigrate: %v", err)
	}

	data, _ := os.ReadFile(filepath.Join(dir, "scenes/level1.scene.json"))
	var scene map[string]interface{}
	json.Unmarshal(data, &scene)
	ents := scene["entities"].([]interface{})
	comp := ents[0].(map[string]interface{})["components"].(map[string]interface{})
	sc := comp["ScriptComponent"].(map[string]interface{})
	if sc["script"] != "assets/scripts/player.ts" {
		t.Fatalf("expected script ref rewritten to .ts, got %v", sc["script"])
	}
}

func TestMigrateUpdatesManifestAssets(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	initRepo(t, dir)

	if err := runMigrate(dir, &bytes.Buffer{}, false, false); err != nil {
		t.Fatalf("runMigrate: %v", err)
	}

	man, err := manifest.LoadManifest(filepath.Join(dir, ".jm.json"))
	if err != nil {
		t.Fatalf("LoadManifest: %v", err)
	}
	want := []string{"assets/scripts/player.ts"}
	if !equalStrings(man.Assets, want) {
		t.Fatalf("assets mismatch: got %v want %v", man.Assets, want)
	}
}

func TestMigrateDeletesScriptJsonFiles(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	initRepo(t, dir)

	if err := runMigrate(dir, &bytes.Buffer{}, false, false); err != nil {
		t.Fatalf("runMigrate: %v", err)
	}

	if _, err := os.Stat(filepath.Join(dir, "assets/scripts/player.script.json")); !os.IsNotExist(err) {
		t.Fatalf("expected .script.json to be deleted, stat err = %v", err)
	}
}

func TestMigrateDryRunDoesNotMutate(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	initRepo(t, dir)

	manBefore, _ := os.ReadFile(filepath.Join(dir, ".jm.json"))
	sceneBefore, _ := os.ReadFile(filepath.Join(dir, "scenes/level1.scene.json"))
	sjBefore, _ := os.ReadFile(filepath.Join(dir, "assets/scripts/player.script.json"))

	var stdout bytes.Buffer
	if err := runMigrate(dir, &stdout, true, false); err != nil {
		t.Fatalf("runMigrate: %v", err)
	}

	manAfter, _ := os.ReadFile(filepath.Join(dir, ".jm.json"))
	sceneAfter, _ := os.ReadFile(filepath.Join(dir, "scenes/level1.scene.json"))
	sjAfter, _ := os.ReadFile(filepath.Join(dir, "assets/scripts/player.script.json"))

	if !bytes.Equal(manBefore, manAfter) {
		t.Fatal("dry-run changed manifest")
	}
	if !bytes.Equal(sceneBefore, sceneAfter) {
		t.Fatal("dry-run changed scene")
	}
	if !bytes.Equal(sjBefore, sjAfter) {
		t.Fatal("dry-run deleted .script.json")
	}
	if !strings.Contains(stdout.String(), "DRY RUN") {
		t.Fatalf("expected DRY RUN banner, got: %s", stdout.String())
	}
	if !strings.Contains(stdout.String(), "assets/scripts/player.ts") {
		t.Fatalf("expected planned target path in stdout, got: %s", stdout.String())
	}
}

func TestMigrateIsIdempotent(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	initRepo(t, dir)

	if err := runMigrate(dir, &bytes.Buffer{}, false, false); err != nil {
		t.Fatalf("first runMigrate: %v", err)
	}

	// Second invocation should detect "nothing to migrate" and exit cleanly.
	var stdout bytes.Buffer
	if err := runMigrate(dir, &stdout, false, true /*force, since first run dirtied tree*/); err != nil {
		t.Fatalf("second runMigrate: %v", err)
	}
	if !strings.Contains(stdout.String(), "nothing to migrate") {
		t.Fatalf("expected 'nothing to migrate', got: %s", stdout.String())
	}
}

func TestMigrateAbortsOnMissingTsTarget(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	initRepo(t, dir)

	// Delete the .ts target so the .script.json's `script` field dangles.
	os.Remove(filepath.Join(dir, "assets/scripts/player.ts"))
	// Re-init so the tree is clean again after the deletion.
	cmd := exec.Command("git", "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-aqm", "rm ts")
	cmd.Dir = dir
	cmd.CombinedOutput()

	err := runMigrate(dir, &bytes.Buffer{}, false, false)
	if err == nil {
		t.Fatal("expected error for missing .ts target")
	}
	var me *migrateError
	if !errors.As(err, &me) || me.code != migrateUserError {
		t.Fatalf("expected migrateUserError, got %v", err)
	}
	if !strings.Contains(err.Error(), "broken target") {
		t.Fatalf("expected 'broken target' in message, got: %v", err)
	}

	// Tree must be unchanged: .script.json still on disk, manifest unchanged.
	if _, statErr := os.Stat(filepath.Join(dir, "assets/scripts/player.script.json")); statErr != nil {
		t.Fatal(".script.json should not have been deleted on abort")
	}
}

func TestMigrateAbortsOnDirtyGitWithoutForce(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	initRepo(t, dir)

	// Dirty the tree.
	mustWrite(t, filepath.Join(dir, "scratch.txt"), []byte("uncommitted"))

	err := runMigrate(dir, &bytes.Buffer{}, false, false)
	if err == nil {
		t.Fatal("expected dirty-tree error")
	}
	var me *migrateError
	if !errors.As(err, &me) || me.code != migrateDirtyTree {
		t.Fatalf("expected migrateDirtyTree, got %v", err)
	}
}

func TestMigrateProceedsInNonGitRepoWithWarning(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	// No initRepo — no .git/ directory at all.

	var stdout bytes.Buffer
	if err := runMigrate(dir, &stdout, false, false); err != nil {
		t.Fatalf("runMigrate: %v", err)
	}
	if !strings.Contains(stdout.String(), "not a git repository") {
		t.Fatalf("expected non-git warning, got: %s", stdout.String())
	}
}

func TestMigrateGitignoreAppendIsIdempotent(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	mustWrite(t, filepath.Join(dir, ".gitignore"), []byte("build/\n"))
	initRepo(t, dir)

	if err := runMigrate(dir, &bytes.Buffer{}, false, false); err != nil {
		t.Fatalf("runMigrate: %v", err)
	}

	data, _ := os.ReadFile(filepath.Join(dir, ".gitignore"))
	if strings.Count(string(data), "build/") != 1 {
		t.Fatalf("build/ duplicated in gitignore: %s", data)
	}
	if !strings.Contains(string(data), "*.jm") {
		t.Fatalf("*.jm not appended: %s", data)
	}

	// Run again with --force (tree dirty after first run).
	if err := runMigrate(dir, &bytes.Buffer{}, false, true); err != nil {
		t.Fatalf("second runMigrate: %v", err)
	}
	data2, _ := os.ReadFile(filepath.Join(dir, ".gitignore"))
	if !bytes.Equal(data, data2) {
		t.Fatalf("gitignore changed on idempotent run: %q vs %q", data, data2)
	}
}

func TestMigrateCreatesGitignoreWhenAbsent(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	initRepo(t, dir)

	if err := runMigrate(dir, &bytes.Buffer{}, false, false); err != nil {
		t.Fatalf("runMigrate: %v", err)
	}

	data, err := os.ReadFile(filepath.Join(dir, ".gitignore"))
	if err != nil {
		t.Fatalf("expected .gitignore created: %v", err)
	}
	if !strings.Contains(string(data), "build/") || !strings.Contains(string(data), "*.jm") {
		t.Fatalf("missing expected lines in gitignore: %s", data)
	}
}

func TestMigrateExitsOnNotAJourneymanProject(t *testing.T) {
	dir := t.TempDir()
	// No .jm.json scaffolded.
	err := runMigrate(dir, &bytes.Buffer{}, false, false)
	if err == nil {
		t.Fatal("expected error for missing .jm.json")
	}
	var me *migrateError
	if !errors.As(err, &me) || me.code != migrateUserError {
		t.Fatalf("expected migrateUserError, got %v", err)
	}
	if !strings.Contains(err.Error(), "Journeyman project") {
		t.Fatalf("expected 'Journeyman project' in message, got: %v", err)
	}
}

func TestMigrateAbortsOnMalformedSceneJson(t *testing.T) {
	dir := t.TempDir()
	scaffold(t, dir)
	mustWrite(t, filepath.Join(dir, "scenes/level1.scene.json"), []byte("{not valid"))
	initRepo(t, dir)

	err := runMigrate(dir, &bytes.Buffer{}, false, false)
	if err == nil {
		t.Fatal("expected malformed scene JSON error")
	}
	if !strings.Contains(err.Error(), "level1.scene.json") {
		t.Fatalf("expected scene path in error, got: %v", err)
	}
}

func equalStrings(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
