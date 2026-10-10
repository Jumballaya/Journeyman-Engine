package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestEditorUnsavedReadsALiveSessionOnly(t *testing.T) {
	root := t.TempDir()
	os.MkdirAll(filepath.Join(root, ".jm"), 0o755)
	write := func(pid int, file string, age time.Duration) {
		at := time.Now().Add(-age).Unix()
		os.WriteFile(filepath.Join(root, ".jm", fmt.Sprintf("editor-session-%d.json", pid)),
			[]byte(fmt.Sprintf(`{"open": [%q], "unsaved": [%q], "updated": %d}`, file, file, at)), 0o644)
	}
	if got := editorUnsaved(root); got != nil {
		t.Errorf("no session: %v", got)
	}
	write(1, "scenes/a.scene.json", 2*time.Second)
	write(2, "scenes/b.scene.json", 2*time.Second) // a second editor on the same project
	if got := editorUnsaved(root); strings.Join(got, ",") != "scenes/a.scene.json,scenes/b.scene.json" {
		t.Errorf("live sessions: %v", got)
	}
	write(1, "scenes/a.scene.json", time.Minute) // crashed a minute ago
	write(2, "scenes/b.scene.json", -time.Hour)  // written before the clock went back an hour
	if got := editorUnsaved(root); got != nil {
		t.Errorf("stale sessions: %v", got)
	}
}
