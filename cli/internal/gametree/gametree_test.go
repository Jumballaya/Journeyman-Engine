package gametree

import (
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"testing"
)

func TestWhatJmMakesIsSkippedAnyCase(t *testing.T) {
	for name, skip := range map[string]bool{
		"build": true, "BUILD": true, "build.next": true, "build.next.modules": true, "dist": true, "logs": true, "frames": true,
		"build/assets": true, "node_modules": true, ".git": true, ".jm/plays": true, "assets/scripts/node_modules": true, "assets/.cache": true,
		"assets": false, "scenes": false, "builds": false, "my-dist": false, ".": false,
		"assets/frames": false, "assets/build": false, // deeper, they're the game's
	} {
		if Skip(name) != skip {
			t.Errorf("Skip(%q) = %v", name, !skip)
		}
	}
}

// The editor's list (editor/Project.cpp generatedFolder) is jm's.
func TestTheEditorSkipsWhatJmMakes(t *testing.T) {
	src, err := os.ReadFile(filepath.Join("..", "..", "..", "editor", "Project.cpp"))
	if err != nil {
		t.Skip("no editor source here:", err)
	}
	set := regexp.MustCompile(`kGenerated = \{([^}]*)\}`).FindSubmatch(src)
	if set == nil {
		t.Fatal("editor/Project.cpp has no kGenerated set")
	}
	var editor []string
	for _, m := range regexp.MustCompile(`"([^"]+)"`).FindAllSubmatch(set[1], -1) {
		editor = append(editor, string(m[1]))
	}
	made := slices.Clone(Made)
	slices.Sort(editor)
	slices.Sort(made)
	if !slices.Equal(editor, made) {
		t.Errorf("the editor skips %v; jm makes %v", editor, made)
	}
}
