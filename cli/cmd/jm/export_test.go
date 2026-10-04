package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestClassifyNewAssetTypes(t *testing.T) {
	dir := t.TempDir()
	cases := map[string]string{
		"assets/ui/menu.ui.html":    "ui",
		"assets/ui/common.css":      "stylesheet",
		"assets/shaders/crt.frag":   "shader",
		"assets/fonts/pixel.ttf":    "font",
		"assets/game.bindings.json": "bindings",
		"assets/sounds/boom.wav":    "audio",
	}
	for rel, want := range cases {
		abs := filepath.Join(dir, rel)
		if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(abs, []byte("x"), 0o644); err != nil {
			t.Fatal(err)
		}
		e, err := classify(rel, abs, dir, nil, true)
		if err != nil {
			t.Fatalf("%s: %v", rel, err)
		}
		if e == nil || e.Type != want {
			t.Fatalf("%s: got %+v, want type %q", rel, e, want)
		}
	}
}

func TestFindEngineSearchesBuildDirThenProjectRoot(t *testing.T) {
	root := t.TempDir()
	build := filepath.Join(root, "build")
	enginePath := filepath.Join(root, "bin", "engine")
	if err := os.MkdirAll(filepath.Dir(enginePath), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(build, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(enginePath, []byte("#!/bin/sh\n"), 0o755); err != nil {
		t.Fatal(err)
	}

	// Authored relative to the project root.
	got, err := resolveEnginePath("bin/engine", filepath.Join(build, ".jm.json"))
	if err != nil || filepath.Clean(got) != filepath.Clean(enginePath) {
		t.Fatalf("project-root relative: got %q, %v", got, err)
	}
	// Legacy: relative to build/.
	got, err = resolveEnginePath("../bin/engine", filepath.Join(build, ".jm.json"))
	if err != nil || filepath.Clean(got) != filepath.Clean(enginePath) {
		t.Fatalf("build relative: got %q, %v", got, err)
	}
	// Archives resolve the same way from their directory.
	got, err = resolveEnginePathArchive("bin/engine", filepath.Join(build, "game.jm"))
	if err != nil || filepath.Clean(got) != filepath.Clean(enginePath) {
		t.Fatalf("archive: got %q, %v", got, err)
	}
	if _, err := resolveEnginePath("bin/missing", filepath.Join(build, ".jm.json")); err == nil {
		t.Fatal("expected an error for a missing engine")
	}
}

func TestExportNameAndPlist(t *testing.T) {
	if got := exportName(` Strike: Wing/1942 `); got != "Strike Wing1942" {
		t.Fatalf("exportName: %q", got)
	}
	if got := exportName("   "); got != "Game" {
		t.Fatalf("exportName empty: %q", got)
	}
	plist := infoPlist("A & B", "A & B", "com.x.y", "", "AppIcon")
	for _, want := range []string{"<string>A &amp; B</string>", "<string>1.0</string>", "CFBundleIconFile"} {
		if !strings.Contains(plist, want) {
			t.Fatalf("plist missing %q:\n%s", want, plist)
		}
	}
}
