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
		e, err := classify(rel, abs, dir, true)
		if err != nil {
			t.Fatalf("%s: %v", rel, err)
		}
		if e == nil || e.Type != want {
			t.Fatalf("%s: got %+v, want type %q", rel, e, want)
		}
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

// Games don't name the engine: $JM_ENGINE, else beside jm (a release, or
// build/bin), else PATH; the server is beside the engine.
func TestFindBinaryOrder(t *testing.T) {
	dir := t.TempDir()
	engine := filepath.Join(dir, "bin", exeName("journeyman_engine"))
	os.MkdirAll(filepath.Dir(engine), 0o755)
	os.WriteFile(engine, []byte("x"), 0o755)
	saved := executablePath
	executablePath = func() (string, error) { return filepath.Join(dir, "bin", "jm"), nil }
	t.Cleanup(func() { executablePath = saved })
	t.Setenv("PATH", t.TempDir())
	t.Setenv("JM_ENGINE", "")
	t.Setenv("JM_SERVER", "")

	if got, err := resolveEnginePath(); err != nil || got != engine {
		t.Fatalf("beside jm: %q %v", got, err)
	}
	other := filepath.Join(dir, "other", exeName("journeyman_engine"))
	os.MkdirAll(filepath.Dir(other), 0o755)
	os.WriteFile(other, []byte("x"), 0o755)
	t.Setenv("JM_ENGINE", other)
	if got, err := resolveEnginePath(); err != nil || got != other {
		t.Fatalf("JM_ENGINE wins: %q %v", got, err)
	}
	t.Setenv("JM_ENGINE", filepath.Join(dir, "nope"))
	if _, err := resolveEnginePath(); err == nil || !strings.Contains(err.Error(), "JM_ENGINE") {
		t.Fatalf("a JM_ENGINE that isn't there is an error naming it: %v", err)
	}
	t.Setenv("JM_ENGINE", "")
	if _, err := resolveServerPath(); err == nil || !strings.Contains(err.Error(), "JM_SERVER") {
		t.Fatalf("no server anywhere: %v", err)
	}
	server := filepath.Join(dir, "bin", exeName("journeyman_server"))
	os.WriteFile(server, []byte("x"), 0o755)
	if got, err := resolveServerPath(); err != nil || got != server {
		t.Fatalf("server beside the engine: %q %v", got, err)
	}
}
