package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/toolchain"
)

// fakeAsc is a toolchain whose compiler logs each script it compiles and
// writes "wasm"; a script named say*.ts warns, fail*.ts doesn't compile.
func fakeAsc(t *testing.T) (toolchain.Toolchain, func() []string) {
	dir := t.TempDir()
	log := filepath.Join(dir, "log")
	node := filepath.Join(dir, "node")
	os.WriteFile(node, []byte(`#!/bin/sh
while [ $# -gt 0 ]; do [ "$1" = --outFile ] && out=$2; shift; done
echo "$out" >> "`+log+`"
case "$out" in *fail*) echo "ERROR TS1005"; exit 1;; *say*) echo "WARNING AS201";; esac
mkdir -p "$(dirname "$out")" && printf wasm > "$out"
`), 0o755)
	return toolchain.Toolchain{Node: node, ASC: dir}, func() []string {
		data, _ := os.ReadFile(log)
		return strings.Fields(string(data))
	}
}

// A script compiled cleanly is compiled once while what compiling reads is
// the same (a scene edited); its script changed, or one that warned, again.
func TestAScriptCompilesOnceUntilAScriptChanges(t *testing.T) {
	tc, compiled := fakeAsc(t)
	root := t.TempDir()
	sources := map[string]string{"assets/scripts/a.ts": "1", "assets/scripts/say.ts": "1", "scenes/main.scene.json": "1"}
	build := func() *report {
		out := filepath.Join(root, "build.next")
		os.RemoveAll(out)
		r := &report{category: "build"}
		compileScripts(r, tc, root, openScriptCache(root, sources, tc), []compile{
			{script: "assets/scripts/a.ts", out: filepath.Join(out, "a.ts")},
			{script: "assets/scripts/say.ts", out: filepath.Join(out, "say.ts")},
			{script: "assets/scripts/fail.ts", out: filepath.Join(out, "fail.ts"), module: true}})
		if data, _ := os.ReadFile(filepath.Join(out, "a.ts")); string(data) != "wasm" {
			t.Errorf("a.ts built as %q", data)
		}
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
	sources["scenes/main.scene.json"] = "2"
	if r := build(); r.errors != 1 {
		t.Errorf("a kept build: errors %d", r.errors)
	}
	if count("a.ts") != 1 || count("say.ts") != 2 || count("fail.ts") != 2 {
		t.Errorf("a scene edited compiled %v", compiled())
	}
	sources["assets/scripts/say.ts"] = "2"
	build()
	if count("a.ts") != 2 {
		t.Errorf("a script changed compiled %v", compiled())
	}
	if kept, _ := os.ReadDir(filepath.Join(root, ".jm", scriptCacheDir)); len(kept) != 1 {
		t.Errorf("kept %d sources' compiles: the last's alone", len(kept))
	}
}
