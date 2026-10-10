package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestWatchSeesWhatTheBuildReadsNotWhatRunsWrite(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{"name": "T", "entryScene": "scenes/main.scene.json", "scenes": ["scenes/main.scene.json"], "assets": ["assets/game/**"]}`), 0o644)
	for _, f := range []string{"scenes/main.scene.json", "assets/sources/ship.png", "build/a.png", "logs/engine.log", "frames/frame_00060.png", ".jm/plays/x", "assets/scripts/node_modules/m.js"} {
		os.MkdirAll(filepath.Dir(filepath.Join(root, f)), 0o755)
		os.WriteFile(filepath.Join(root, f), []byte("x"), 0o644)
	}
	times := sourceTimes(root)
	if len(times) != 3 {
		t.Errorf("watched %v, want only the manifest, the scene and the atlas source image", times)
	}
	if sameTimes(times, map[string]time.Time{}) {
		t.Error("different sets read as the same")
	}
}
