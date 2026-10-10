package main

import (
	"os"
	"path/filepath"
	"testing"
)

// A second hard link sees in-place writes; it keeps the old bytes only when
// fmt writes a new file and renames it over (no window with a truncated file).
func TestFmtReplacesFilesInsteadOfRewritingThemInPlace(t *testing.T) {
	dir := t.TempDir()
	scene := filepath.Join(dir, "level.scene.json")
	original := `{"name":"level","entities":[]}`
	mustWrite(t, scene, []byte(original))
	link := filepath.Join(dir, "link.json")
	if err := os.Link(scene, link); err != nil {
		t.Skip("no hard links here:", err)
	}
	if err := fmtCmd.RunE(fmtCmd, []string{scene}); err != nil {
		t.Fatal(err)
	}
	if data, _ := os.ReadFile(scene); string(data) == original {
		t.Fatal("scene wasn't formatted")
	}
	if data, _ := os.ReadFile(link); string(data) != original {
		t.Fatalf("fmt rewrote the file in place: %q", data)
	}
}
