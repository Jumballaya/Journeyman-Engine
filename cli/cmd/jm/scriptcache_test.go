package main

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/toolchain"
)

// fakeAsc is a toolchain whose compiler logs each script it compiles and
// writes "wasm" (and a source map for mapped*.ts); say*.ts warns, fail*.ts
// doesn't compile.
func fakeAsc(t *testing.T) (toolchain.Toolchain, func() []string) {
	if runtime.GOOS == "windows" {
		t.Skip("the fake compiler is a sh script")
	}
	dir := t.TempDir()
	log := filepath.Join(dir, "log")
	node := filepath.Join(dir, "node")
	os.WriteFile(node, []byte(`#!/bin/sh
while [ $# -gt 0 ]; do [ "$1" = --outFile ] && out=$2; shift; done
echo "$out" >> "`+log+`"
case "$out" in *fail*) echo "ERROR TS1005"; exit 1;; *say*) echo "WARNING AS201" >&2;; esac
mkdir -p "$(dirname "$out")" && printf wasm > "$out"
case "$out" in *mapped*) printf map > "${out%.ts}.wasm.map";; esac
`), 0o755)
	return toolchain.Toolchain{Node: node, ASC: dir}, func() []string {
		data, _ := os.ReadFile(log)
		return strings.Fields(string(data))
	}
}

// A script compiled cleanly is compiled once while nothing compiling may read
// changes (a scene edited); a script, a config (or the asconfig's base, even
// outside the game), the manifest (its libraries) changed, or one that
// warned, again. Its source map comes back with it.
func TestAScriptCompilesOnceUntilWhatCompilingReadsChanges(t *testing.T) {
	tc, compiled := fakeAsc(t)
	root := t.TempDir()
	sources := map[string]string{"assets/scripts/a.ts": "1", "assets/scripts/say.ts": "1", "scenes/main.scene.json": "1",
		"assets/hero.png": "1", "asconfig.base.json": "1", ".jm.json": "1"}
	out := filepath.Join(root, "build.next")
	build := func() *report {
		os.RemoveAll(out)
		r := &report{category: "build"}
		compileScripts(r, tc, root, openScriptCache(root, sources, tc), []compile{
			{script: "assets/scripts/a.ts", out: filepath.Join(out, "a.ts")},
			{script: "assets/scripts/mapped.ts", out: filepath.Join(out, "mapped.ts")},
			{script: "assets/scripts/say.ts", out: filepath.Join(out, "say.ts")},
			{script: "assets/scripts/fail.ts", out: filepath.Join(out, "fail.ts"), module: true}})
		return r
	}
	count := func(name string) int {
		n := 0
		for _, f := range compiled() {
			if filepath.Base(f) == name {
				n++
			}
		}
		return n
	}
	if r := build(); r.errors != 1 {
		t.Errorf("errors %d: fail.ts's alone", r.errors)
	}
	sources["scenes/main.scene.json"], sources["assets/hero.png"] = "2", "2"
	if r := build(); r.errors != 1 {
		t.Errorf("a kept build: errors %d", r.errors)
	}
	if count("a.ts") != 1 || count("say.ts") != 2 || count("fail.ts") != 2 {
		t.Errorf("a scene and an image edited compiled %v", compiled())
	}
	for file, want := range map[string]string{"a.ts": "wasm", "mapped.ts": "wasm", "mapped.wasm.map": "map"} {
		if data, _ := os.ReadFile(filepath.Join(out, file)); string(data) != want {
			t.Errorf("kept %s is %q", file, data)
		}
	}
	for i, changed := range []string{"assets/scripts/say.ts", "asconfig.base.json", ".jm.json"} {
		sources[changed] = "2"
		build()
		if count("a.ts") != i+2 {
			t.Errorf("%s changed: compiled %v", changed, compiled())
		}
	}
	base := filepath.Join(t.TempDir(), "shared.json") // beside the game: no source of it
	os.MkdirAll(filepath.Join(root, "assets", "scripts"), 0o755)
	os.WriteFile(filepath.Join(root, "assets", "scripts", "asconfig.json"), []byte(`{"extends": "`+filepath.ToSlash(base)+`"}`), 0o644)
	os.WriteFile(base, []byte(`{"options": {"noAssert": true}}`), 0o644)
	build()
	os.WriteFile(base, []byte(`{"options": {"noAssert": false}}`), 0o644)
	build()
	if count("a.ts") != 6 {
		t.Errorf("the asconfig's base changed: compiled %v", compiled())
	}
	// A chain of bases, however long: its last one changed compiles again.
	chain := t.TempDir()
	for i := range 10 {
		next := `{"extends": "./` + string(rune('a'+i+1)) + `.json"}`
		os.WriteFile(filepath.Join(chain, string(rune('a'+i))+".json"), []byte(next), 0o644)
	}
	os.WriteFile(base, []byte(`{"extends": "`+filepath.ToSlash(filepath.Join(chain, "a.json"))+`"}`), 0o644)
	build()
	os.WriteFile(filepath.Join(chain, "k.json"), []byte(`{"options": {"noAssert": true}}`), 0o644)
	build()
	if count("a.ts") != 8 {
		t.Errorf("the chain's last base changed: compiled %v", compiled())
	}
	// What the cache can't follow it doesn't keep: a package's config, a file asc writes but the map.
	for _, config := range []string{`{"extends": "@studio/asconfig/base.json"}`, `{"options": {"sourceMap": "custom.map"}}`,
		`{"targets": {"release": {"bindings": "esm"}}}`} {
		os.WriteFile(base, []byte(config), 0o644)
		before := count("a.ts")
		build()
		build()
		if count("a.ts") != before+2 {
			t.Errorf("%s: compiled %d times in two builds", config, count("a.ts")-before)
		}
	}
	if kept, _ := os.ReadDir(filepath.Join(root, ".jm", scriptCacheDir)); len(kept) != 1 {
		t.Errorf("kept %d sources' compiles: the last's alone", len(kept))
	}
}
