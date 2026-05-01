package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// writeFile creates intermediate dirs and writes contents to path.
func writeFile(t *testing.T, path string, data []byte) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatalf("mkdir %s: %v", filepath.Dir(path), err)
	}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

// writeManifest writes a valid .jm.json into buildDir.
func writeManifest(t *testing.T, buildDir, name string) {
	t.Helper()
	man := manifest.GameManifest{
		Name:       name,
		Version:    "0.0.1",
		EnginePath: "/usr/bin/true",
		EntryScene: "scenes/level1.scene.json",
		Scenes:     []string{"scenes/level1.scene.json"},
		Assets:     []string{},
	}
	data, _ := json.Marshal(man)
	writeFile(t, filepath.Join(buildDir, ".jm.json"), data)
}

// writeScript writes a .script.json + .wasm pair into buildDir, returning
// the .script.json's path relative to buildDir.
func writeScript(t *testing.T, buildDir, baseRel string, imports []string, wasmPayload []byte) string {
	t.Helper()
	scriptRel := baseRel + ".script.json"
	wasmRel := baseRel + ".wasm"
	sa := manifest.ScriptAsset{
		Name:    filepath.Base(baseRel),
		Script:  baseRel + ".ts",
		Binary:  wasmRel,
		Imports: imports,
		Exposed: []string{},
	}
	data, _ := json.MarshalIndent(sa, "", "  ")
	writeFile(t, filepath.Join(buildDir, scriptRel), data)
	writeFile(t, filepath.Join(buildDir, wasmRel), wasmPayload)
	return scriptRel
}

// readArchive opens the archive at path and returns the *Archive.
func readArchive(t *testing.T, path string) *archive.Archive {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Fatalf("open archive %s: %v", path, err)
	}
	t.Cleanup(func() { f.Close() })
	info, err := f.Stat()
	if err != nil {
		t.Fatalf("stat: %v", err)
	}
	arc, err := archive.ReadArchive(f, info.Size())
	if err != nil {
		t.Fatalf("ReadArchive: %v", err)
	}
	return arc
}

func TestPackProducesArchiveWithManifestAndScripts(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeScript(t, buildDir, "assets/scripts/player", []string{"abort"}, []byte("FAKE-WASM"))
	writeFile(t, filepath.Join(buildDir, "scenes/level1.scene.json"), []byte(`{"name":"l1"}`))

	out := filepath.Join(tmp, "out.jm")
	if err := runPack(buildDir, out, false); err != nil {
		t.Fatalf("runPack: %v", err)
	}

	arc := readArchive(t, out)
	want := []string{".jm.json", "assets/scripts/player.script.json", "scenes/level1.scene.json"}
	got := arc.Entries()
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("entries: got %v want %v", got, want)
	}
}

func TestPackBundlesWasmIntoScriptEntry(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	wasm := []byte("\x00asm\x01\x00\x00\x00FAKE")
	writeManifest(t, buildDir, "Test Game")
	writeScript(t, buildDir, "assets/scripts/player", []string{"abort"}, wasm)

	out := filepath.Join(tmp, "out.jm")
	if err := runPack(buildDir, out, false); err != nil {
		t.Fatalf("runPack: %v", err)
	}

	arc := readArchive(t, out)
	if arc.Contains("assets/scripts/player.wasm") {
		t.Fatal("standalone .wasm entry should not exist")
	}
	got, err := arc.Read("assets/scripts/player.script.json")
	if err != nil {
		t.Fatalf("Read script entry: %v", err)
	}
	if !bytes.Equal(got, wasm) {
		t.Fatalf("script payload mismatch: got %v want %v", got, wasm)
	}
}

func TestPackPreservesScriptMetadata(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeScript(t, buildDir, "assets/scripts/player", []string{"abort", "__jmLog"}, []byte("FAKE-WASM"))

	out := filepath.Join(tmp, "out.jm")
	if err := runPack(buildDir, out, false); err != nil {
		t.Fatalf("runPack: %v", err)
	}

	// Re-read to inspect metadata: the public reader Read() returns payload but
	// not metadata. Decode the file directly to inspect resolver.
	data, err := os.ReadFile(out)
	if err != nil {
		t.Fatalf("read archive file: %v", err)
	}
	// Parse resolver JSON: header is 32 bytes, payloadOffset+payloadSize from header.
	// Easier: regex-search for the imports field; precision not required here.
	if !bytes.Contains(data, []byte(`"imports"`)) {
		t.Fatal("imports key missing from resolver JSON")
	}
	if !bytes.Contains(data, []byte(`"abort"`)) || !bytes.Contains(data, []byte(`"__jmLog"`)) {
		t.Fatal("expected import names missing from resolver JSON")
	}
}

func TestPackHandlesPrefabFiles(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	prefabBytes := []byte(`{"components":{"Sprite":{}}}`)
	writeManifest(t, buildDir, "Test Game")
	writeFile(t, filepath.Join(buildDir, "assets/prefabs/bullet.prefab.json"), prefabBytes)

	out := filepath.Join(tmp, "out.jm")
	if err := runPack(buildDir, out, false); err != nil {
		t.Fatalf("runPack: %v", err)
	}

	arc := readArchive(t, out)
	got, err := arc.Read("assets/prefabs/bullet.prefab.json")
	if err != nil {
		t.Fatalf("Read prefab: %v", err)
	}
	if !bytes.Equal(got, prefabBytes) {
		t.Fatalf("prefab bytes mismatch")
	}
}

func TestPackErrorsOnUnreferencedWasm(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeFile(t, filepath.Join(buildDir, "stray.wasm"), []byte("FAKE-WASM"))

	out := filepath.Join(tmp, "out.jm")
	err := runPack(buildDir, out, false)
	if err == nil || !strings.Contains(err.Error(), "stray") {
		t.Fatalf("expected stray wasm error, got %v", err)
	}
}

func TestPackErrorsOnDuplicateWasmReference(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeFile(t, filepath.Join(buildDir, "shared.wasm"), []byte("FAKE-WASM"))

	for _, name := range []string{"a", "b"} {
		sa := manifest.ScriptAsset{
			Name:    name,
			Script:  name + ".ts",
			Binary:  "shared.wasm",
			Imports: []string{},
			Exposed: []string{},
		}
		data, _ := json.Marshal(sa)
		writeFile(t, filepath.Join(buildDir, name+".script.json"), data)
	}

	out := filepath.Join(tmp, "out.jm")
	err := runPack(buildDir, out, false)
	if err == nil || !strings.Contains(err.Error(), "shared.wasm") {
		t.Fatalf("expected duplicate wasm reference error, got %v", err)
	}
}

func TestPackErrorsOnMissingBinaryFile(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	sa := manifest.ScriptAsset{
		Name:    "missing",
		Script:  "missing.ts",
		Binary:  "missing.wasm",
		Imports: []string{},
		Exposed: []string{},
	}
	data, _ := json.Marshal(sa)
	writeFile(t, filepath.Join(buildDir, "missing.script.json"), data)

	out := filepath.Join(tmp, "out.jm")
	err := runPack(buildDir, out, false)
	if err == nil || !strings.Contains(err.Error(), "missing.wasm") {
		t.Fatalf("expected missing binary error, got %v", err)
	}
}

func TestPackErrorsOnMalformedScriptJson(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeFile(t, filepath.Join(buildDir, "bad.script.json"), []byte("{not valid json"))

	out := filepath.Join(tmp, "out.jm")
	err := runPack(buildDir, out, false)
	if err == nil {
		t.Fatal("expected malformed script.json error")
	}
}

func TestPackErrorsOnScriptJsonMissingBinaryField(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeFile(t, filepath.Join(buildDir, "noBin.script.json"),
		[]byte(`{"name":"x","script":"x.ts"}`))

	out := filepath.Join(tmp, "out.jm")
	err := runPack(buildDir, out, false)
	if err == nil || !strings.Contains(err.Error(), "binary") {
		t.Fatalf("expected missing binary field error, got %v", err)
	}
}

func TestPackSkipsHiddenFilesSilently(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeFile(t, filepath.Join(buildDir, ".DS_Store"), []byte("garbage"))
	writeFile(t, filepath.Join(buildDir, "scenes/.gitkeep"), []byte(""))

	out := filepath.Join(tmp, "out.jm")
	if err := runPack(buildDir, out, false); err != nil {
		t.Fatalf("runPack: %v", err)
	}

	arc := readArchive(t, out)
	for _, k := range arc.Entries() {
		if strings.HasSuffix(k, ".DS_Store") || strings.HasSuffix(k, ".gitkeep") {
			t.Fatalf("hidden file %s should have been skipped", k)
		}
	}
}

func TestPackErrorsOnUnknownExtensionInStrictMode(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeFile(t, filepath.Join(buildDir, "weird.xyz"), []byte("?"))

	out := filepath.Join(tmp, "out.jm")
	if err := runPack(buildDir, out, false); err != nil {
		t.Fatalf("non-strict should not error: %v", err)
	}

	out2 := filepath.Join(tmp, "out2.jm")
	err := runPack(buildDir, out2, true)
	if err == nil || !strings.Contains(err.Error(), "unrecognized") {
		t.Fatalf("strict mode: expected unrecognized error, got %v", err)
	}
}

func TestPackKeysAreCanonicalAndPlatformPortable(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeFile(t, filepath.Join(buildDir, "deeply/nested/path/file.scene.json"), []byte(`{}`))

	out := filepath.Join(tmp, "out.jm")
	if err := runPack(buildDir, out, false); err != nil {
		t.Fatalf("runPack: %v", err)
	}

	arc := readArchive(t, out)
	for _, k := range arc.Entries() {
		if strings.Contains(k, "\\") {
			t.Fatalf("key %q contains backslash", k)
		}
		if strings.HasPrefix(k, "./") {
			t.Fatalf("key %q has ./ prefix", k)
		}
	}
}

func TestPackOutputIsDeterministic(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "Test Game")
	writeScript(t, buildDir, "assets/scripts/a", []string{"abort"}, []byte("WASM-A"))
	writeScript(t, buildDir, "assets/scripts/b", []string{}, []byte("WASM-B"))
	writeFile(t, filepath.Join(buildDir, "scenes/level1.scene.json"), []byte(`{"a":1}`))

	out1 := filepath.Join(tmp, "out1.jm")
	out2 := filepath.Join(tmp, "out2.jm")
	if err := runPack(buildDir, out1, false); err != nil {
		t.Fatalf("first pack: %v", err)
	}
	if err := runPack(buildDir, out2, false); err != nil {
		t.Fatalf("second pack: %v", err)
	}
	a, _ := os.ReadFile(out1)
	b, _ := os.ReadFile(out2)
	if !bytes.Equal(a, b) {
		t.Fatalf("non-deterministic output: %d vs %d bytes", len(a), len(b))
	}
}

func TestPackUsesSlugFromManifestNameAsDefaultOutput(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "My Demo Game")

	if err := runPack(buildDir, "", false); err != nil {
		t.Fatalf("runPack: %v", err)
	}

	expected := filepath.Join(buildDir, "my-demo-game.jm")
	if _, err := os.Stat(expected); err != nil {
		t.Fatalf("expected output %s: %v", expected, err)
	}
}

func TestPackEmptyManifestNameFallsBackToGame(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	writeManifest(t, buildDir, "")

	if err := runPack(buildDir, "", false); err != nil {
		t.Fatalf("runPack: %v", err)
	}

	expected := filepath.Join(buildDir, "game.jm")
	if _, err := os.Stat(expected); err != nil {
		t.Fatalf("expected fallback output %s: %v", expected, err)
	}
}

func TestPackErrorsOnMissingManifest(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	if err := os.MkdirAll(buildDir, 0o755); err != nil {
		t.Fatalf("mkdir: %v", err)
	}

	out := filepath.Join(tmp, "out.jm")
	err := runPack(buildDir, out, false)
	if err == nil || !strings.Contains(err.Error(), ".jm.json") {
		t.Fatalf("expected missing manifest error, got %v", err)
	}
}

func TestPackErrorsOnNonexistentBuildDir(t *testing.T) {
	tmp := t.TempDir()
	missing := filepath.Join(tmp, "does-not-exist")
	out := filepath.Join(tmp, "out.jm")
	err := runPack(missing, out, false)
	if err == nil || !strings.Contains(err.Error(), missing) {
		t.Fatalf("expected nonexistent dir error, got %v", err)
	}
}

func TestSlugify(t *testing.T) {
	cases := []struct{ in, want string }{
		{"My Demo Game", "my-demo-game"},
		{"  Edge   Spaces  ", "edge-spaces"},
		{"!!!", "game"},
		{"", "game"},
		{"already-slug", "already-slug"},
	}
	for _, c := range cases {
		got := slugify(c.in)
		if got != c.want {
			t.Errorf("slugify(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}
