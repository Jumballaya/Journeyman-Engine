package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// nodeMajorVersion is not unit-tested because it shells to `node`. Tested
// indirectly via TestCheckBuildPrereqs (not present here — checkBuildPrereqs
// also shells out, and refactoring for injectability is out of scope for this
// task).

// ---------------------------------------------------------------------------
// validateRelativePath
// ---------------------------------------------------------------------------

func TestValidateRelativePath(t *testing.T) {
	type tc struct {
		name        string
		path        string
		wantErr     bool
		errContains string // substring expected in the error message; "" means skip the check
	}

	cases := []tc{
		// PASS cases.
		{"simple file", "foo.ts", false, ""},
		{"nested path", "a/b/c.wasm", false, ""},
		{"double-dot in middle of segment", "foo..bar", false, ""},
		{"double-dot prefix", "..foo", false, ""},
		{"dot-slash prefix collapses", "./bar", false, ""},
		{"single dot collapses to dot — allowed", ".", false, ""},

		// `foo/../bar` collapses via filepath.Clean to `bar` BEFORE the segment
		// walk, so it passes — the cleaned form contains no `..` segment and
		// the path is contained within the tree. Pin this so anyone tightening
		// the function (e.g. checking the raw input) updates the test.
		{"interior traversal collapses to safe path", "foo/../bar", false, ""},

		// FAIL cases.
		{"empty string", "", true, "empty"},
		{"parent traversal", "../foo", true, "../foo"},
		{"escaping traversal", "a/../../b", true, "a/../../b"},
	}

	if runtime.GOOS != "windows" {
		// `filepath.IsAbs` treats `/etc/passwd` as absolute on POSIX only.
		cases = append(cases, tc{"absolute posix path", "/etc/passwd", true, "/etc/passwd"})
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			err := validateRelativePath(c.path)
			if c.wantErr && err == nil {
				t.Fatalf("validateRelativePath(%q): expected error, got nil", c.path)
			}
			if !c.wantErr && err != nil {
				t.Fatalf("validateRelativePath(%q): unexpected error: %v", c.path, err)
			}
			if c.wantErr && c.errContains != "" && !strings.Contains(err.Error(), c.errContains) {
				t.Fatalf("validateRelativePath(%q): error %q should contain %q",
					c.path, err.Error(), c.errContains)
			}
		})
	}
}

// ---------------------------------------------------------------------------
// walkSceneAndRewrite
// ---------------------------------------------------------------------------

// makeScene returns a deeply-nested map[string]interface{} that mimics a real
// scene: a top-level object with an `entities` array; each entity has a
// `components` map, and one entity has a `ScriptComponent` referencing a
// .script.json. Constructed as a literal so reads against the same instance
// after walkSceneAndRewrite mutates it.
func makeScene(scriptRef string) map[string]interface{} {
	return map[string]interface{}{
		"name": "level1",
		"entities": []interface{}{
			map[string]interface{}{
				"name": "player",
				"components": map[string]interface{}{
					"Transform": map[string]interface{}{"x": 0, "y": 0},
					"ScriptComponent": map[string]interface{}{
						"script": scriptRef,
					},
				},
			},
			// Entity without ScriptComponent — must be left untouched.
			map[string]interface{}{
				"name": "ground",
				"components": map[string]interface{}{
					"Sprite": map[string]interface{}{"texture": "ground.png"},
				},
			},
			// Non-map child to exercise the array recursion path.
			"loose-string-not-an-entity",
			42,
		},
	}
}

func TestWalkSceneAndRewriteRewritesMatchingScriptRef(t *testing.T) {
	scene := makeScene("assets/scripts/player.script.json")
	rewrites := map[string]string{
		"assets/scripts/player.script.json": "assets/scripts/player.ts",
	}

	walkSceneAndRewrite(scene, rewrites, "scenes/level1.scene.json")

	entities := scene["entities"].([]interface{})
	player := entities[0].(map[string]interface{})
	comps := player["components"].(map[string]interface{})
	sc := comps["ScriptComponent"].(map[string]interface{})

	got := sc["script"].(string)
	if got != "assets/scripts/player.ts" {
		t.Fatalf("script ref not rewritten: got %q", got)
	}

	// Sibling entity untouched.
	ground := entities[1].(map[string]interface{})
	groundComps := ground["components"].(map[string]interface{})
	if _, hasScript := groundComps["ScriptComponent"]; hasScript {
		t.Fatal("walk added ScriptComponent to entity that didn't have one")
	}
}

func TestWalkSceneAndRewriteUntouchedWhenNoMatch(t *testing.T) {
	original := "assets/scripts/missing.script.json"
	scene := makeScene(original)
	rewrites := map[string]string{
		// Different key — no match.
		"assets/scripts/other.script.json": "assets/scripts/other.ts",
	}

	walkSceneAndRewrite(scene, rewrites, "scenes/level1.scene.json")

	got := scene["entities"].([]interface{})[0].(map[string]interface{})["components"].(map[string]interface{})["ScriptComponent"].(map[string]interface{})["script"].(string)
	if got != original {
		t.Fatalf("script ref mutated despite no match: got %q want %q", got, original)
	}
}

func TestWalkSceneAndRewriteHandlesMissingScriptComponent(t *testing.T) {
	scene := map[string]interface{}{
		"entities": []interface{}{
			map[string]interface{}{
				"name": "no-scripts-here",
				"components": map[string]interface{}{
					"Sprite": map[string]interface{}{"texture": "x.png"},
				},
			},
		},
	}
	// Should not panic. Should not mutate.
	walkSceneAndRewrite(scene, map[string]string{"foo": "bar"}, "scenes/x.scene.json")

	comps := scene["entities"].([]interface{})[0].(map[string]interface{})["components"].(map[string]interface{})
	if _, ok := comps["ScriptComponent"]; ok {
		t.Fatal("walk added a ScriptComponent where none existed")
	}
}

func TestWalkSceneAndRewriteHandlesNonMapNonArrayNodes(t *testing.T) {
	// Top-level node is a string. Walk should no-op without panicking.
	defer func() {
		if r := recover(); r != nil {
			t.Fatalf("panic on scalar node: %v", r)
		}
	}()
	walkSceneAndRewrite("scalar string", map[string]string{}, "x")
	walkSceneAndRewrite(123, map[string]string{}, "x")
	walkSceneAndRewrite(nil, map[string]string{}, "x")

	// Mixed nesting: array of scalars + nested map.
	mixed := map[string]interface{}{
		"things": []interface{}{1, "two", true, nil},
	}
	walkSceneAndRewrite(mixed, map[string]string{}, "x")
}

func TestWalkSceneAndRewriteIgnoresNonScriptJsonRefs(t *testing.T) {
	// Bug surface: the function only rewrites refs that end in ".script.json".
	// A `.ts` ref already in the scene must be left alone even if it appears
	// in the rewrite map (it shouldn't, but pin behavior).
	scene := makeScene("assets/scripts/player.ts")
	rewrites := map[string]string{
		"assets/scripts/player.ts": "should-not-be-applied",
	}
	walkSceneAndRewrite(scene, rewrites, "scenes/level1.scene.json")

	got := scene["entities"].([]interface{})[0].(map[string]interface{})["components"].(map[string]interface{})["ScriptComponent"].(map[string]interface{})["script"].(string)
	if got != "assets/scripts/player.ts" {
		t.Fatalf("non-.script.json ref mutated: got %q", got)
	}
}

// ---------------------------------------------------------------------------
// buildLegacyScriptMap
// ---------------------------------------------------------------------------

func TestBuildLegacyScriptMap(t *testing.T) {
	dir := t.TempDir()
	chdir(t, dir)

	// Two valid .script.json entries.
	playerScript := manifest.ScriptAsset{
		Name:    "player",
		Script:  "assets/scripts/player.ts",
		Binary:  "assets/scripts/player.wasm",
		Imports: []string{},
		Exposed: []string{},
	}
	enemyScript := manifest.ScriptAsset{
		Name:    "enemy",
		Script:  "assets/scripts/enemy.ts",
		Binary:  "assets/scripts/enemy.wasm",
		Imports: []string{},
		Exposed: []string{},
	}
	playerData, _ := json.Marshal(playerScript)
	enemyData, _ := json.Marshal(enemyScript)
	writeFile(t, filepath.Join(dir, "assets/scripts/player.script.json"), playerData)
	writeFile(t, filepath.Join(dir, "assets/scripts/enemy.script.json"), enemyData)

	// A malformed .script.json — should be skipped, not crash.
	writeFile(t, filepath.Join(dir, "assets/scripts/broken.script.json"), []byte("{not valid"))

	// A non-existent .script.json (in assets[] but file is missing).
	missingPath := "assets/scripts/missing.script.json"

	// A non-.script.json entry — should be skipped silently.
	writeFile(t, filepath.Join(dir, "assets/textures/player.png"), []byte("FAKE-PNG"))

	assets := []string{
		"assets/scripts/player.script.json",
		"assets/scripts/enemy.script.json",
		"assets/scripts/broken.script.json",
		missingPath,
		"assets/textures/player.png",
	}

	got := buildLegacyScriptMap(assets)

	// Two valid entries should appear, with cleaned slash-form keys & values.
	want := map[string]string{
		"assets/scripts/player.script.json": "assets/scripts/player.ts",
		"assets/scripts/enemy.script.json":  "assets/scripts/enemy.ts",
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("buildLegacyScriptMap result:\n got:  %v\n want: %v", got, want)
	}
}

func TestBuildLegacyScriptMapEmptyAssets(t *testing.T) {
	got := buildLegacyScriptMap(nil)
	if got == nil {
		t.Fatal("expected non-nil empty map")
	}
	if len(got) != 0 {
		t.Fatalf("expected empty map, got %v", got)
	}
}

// ---------------------------------------------------------------------------
// syncEmbeddedRuntime
// ---------------------------------------------------------------------------

func TestSyncEmbeddedRuntime(t *testing.T) {
	root := t.TempDir()
	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("syncEmbeddedRuntime: %v", err)
	}

	runtimeDir := filepath.Join(root, "assets/scripts/node_modules/@jm/runtime")

	// index.ts must exist and contain the expected re-export lines.
	indexPath := filepath.Join(runtimeDir, "index.ts")
	indexBytes, err := os.ReadFile(indexPath)
	if err != nil {
		t.Fatalf("read %s: %v", indexPath, err)
	}
	idx := string(indexBytes)
	for _, want := range []string{
		`from "./ecs"`,
		`from "./inputs"`,
		`from "./audio"`,
		`from "./console"`,
		`from "./time"`,
		`from "./posteffects"`,
		`from "./scene"`,
	} {
		if !strings.Contains(idx, want) {
			t.Fatalf("index.ts missing re-export %q\nfull:\n%s", want, idx)
		}
	}

	// package.json must be extracted with @jm/runtime name.
	pkgPath := filepath.Join(runtimeDir, "package.json")
	pkgBytes, err := os.ReadFile(pkgPath)
	if err != nil {
		t.Fatalf("read %s: %v", pkgPath, err)
	}
	if !strings.Contains(string(pkgBytes), `"@jm/runtime"`) {
		t.Fatalf("package.json missing @jm/runtime name: %s", pkgBytes)
	}
}

func TestSyncEmbeddedRuntimeIsIdempotent(t *testing.T) {
	root := t.TempDir()

	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("first sync: %v", err)
	}
	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("second sync: %v", err)
	}

	// Spot-check that the well-known files survived the second invocation.
	indexPath := filepath.Join(root, "assets/scripts/node_modules/@jm/runtime/index.ts")
	if _, err := os.Stat(indexPath); err != nil {
		t.Fatalf("index.ts missing after second sync: %v", err)
	}
}

func TestSyncEmbeddedRuntimePrunesExtraneousFiles(t *testing.T) {
	root := t.TempDir()

	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("first sync: %v", err)
	}

	// Plant a file that doesn't exist in the embed. Second sync should wipe it.
	extra := filepath.Join(root, "assets/scripts/node_modules/@jm/runtime/extra.ts")
	if err := os.WriteFile(extra, []byte("// stale"), 0o644); err != nil {
		t.Fatalf("plant extra: %v", err)
	}

	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("second sync: %v", err)
	}

	if _, err := os.Stat(extra); !os.IsNotExist(err) {
		t.Fatalf("extra.ts should have been pruned, stat err: %v", err)
	}

	// And the legitimate files still exist.
	if _, err := os.Stat(filepath.Join(root, "assets/scripts/node_modules/@jm/runtime/index.ts")); err != nil {
		t.Fatalf("index.ts missing after prune: %v", err)
	}
}

// ---------------------------------------------------------------------------
// extractWasmMetadata
// ---------------------------------------------------------------------------

// buildMinimalWasm constructs a tiny but valid wasm module: one function
// import named `__jmLog` and one function export named `run`. Mirrors the
// fixture-building approach in internal/wasm/parse_test.go.
func buildMinimalWasm() []byte {
	wasmHeader := []byte{
		0x00, 0x61, 0x73, 0x6d, // \0asm
		0x01, 0x00, 0x00, 0x00, // version 1
	}

	leb128 := func(v uint32) []byte {
		var out []byte
		for {
			b := byte(v & 0x7f)
			v >>= 7
			if v != 0 {
				out = append(out, b|0x80)
			} else {
				out = append(out, b)
				return out
			}
		}
	}

	nameBytes := func(s string) []byte {
		out := leb128(uint32(len(s)))
		return append(out, []byte(s)...)
	}

	section := func(id byte, body []byte) []byte {
		out := []byte{id}
		out = append(out, leb128(uint32(len(body)))...)
		return append(out, body...)
	}

	// type section (id=1): one functype () -> ()
	typeBody := []byte{}
	typeBody = append(typeBody, leb128(1)...) // count
	typeBody = append(typeBody, 0x60)         // functype tag
	typeBody = append(typeBody, leb128(0)...) // params
	typeBody = append(typeBody, leb128(0)...) // results

	// import section (id=2): env.__jmLog (func, type 0)
	importBody := []byte{}
	importBody = append(importBody, leb128(1)...)
	importBody = append(importBody, nameBytes("env")...)
	importBody = append(importBody, nameBytes("__jmLog")...)
	importBody = append(importBody, 0x00)         // kind = func
	importBody = append(importBody, leb128(0)...) // type-index

	// export section (id=7): run -> func index 1
	exportBody := []byte{}
	exportBody = append(exportBody, leb128(1)...)
	exportBody = append(exportBody, nameBytes("run")...)
	exportBody = append(exportBody, 0x00)         // kind = func
	exportBody = append(exportBody, leb128(1)...) // index

	mod := []byte{}
	mod = append(mod, wasmHeader...)
	mod = append(mod, section(1, typeBody)...)
	mod = append(mod, section(2, importBody)...)
	mod = append(mod, section(7, exportBody)...)
	return mod
}

func TestExtractWasmMetadataReturnsImportsAndExports(t *testing.T) {
	dir := t.TempDir()
	wasmPath := filepath.Join(dir, "tiny.wasm")
	if err := os.WriteFile(wasmPath, buildMinimalWasm(), 0o644); err != nil {
		t.Fatalf("write fixture: %v", err)
	}

	md, err := extractWasmMetadata(wasmPath)
	if err != nil {
		t.Fatalf("extractWasmMetadata: %v", err)
	}

	if !reflect.DeepEqual(md.Imports, []string{"__jmLog"}) {
		t.Errorf("Imports: got %v, want [__jmLog]", md.Imports)
	}
	if !reflect.DeepEqual(md.Exposed, []string{"run"}) {
		t.Errorf("Exposed: got %v, want [run]", md.Exposed)
	}
}

func TestExtractWasmMetadataMissingFileWrapsPath(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "does-not-exist.wasm")
	_, err := extractWasmMetadata(missing)
	if err == nil {
		t.Fatal("expected error for missing wasm file, got nil")
	}
	if !strings.Contains(err.Error(), missing) {
		t.Fatalf("error should contain path %q, got: %v", missing, err)
	}
}

func TestExtractWasmMetadataMalformedReturnsError(t *testing.T) {
	dir := t.TempDir()
	bad := filepath.Join(dir, "bad.wasm")
	if err := os.WriteFile(bad, []byte("not-a-wasm-file"), 0o644); err != nil {
		t.Fatalf("write bad fixture: %v", err)
	}

	_, err := extractWasmMetadata(bad)
	if err == nil {
		t.Fatal("expected parse error on garbage bytes, got nil")
	}
	if !strings.Contains(err.Error(), bad) {
		t.Fatalf("error should contain wasm path %q, got: %v", bad, err)
	}
}
