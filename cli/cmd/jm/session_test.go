package main

import (
	"encoding/json"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

func TestServerPackLeavesTheFrontendOut(t *testing.T) {
	tmp := t.TempDir()
	buildDir := filepath.Join(tmp, "build")
	man := map[string]any{
		"name": "Net Game", "version": "1.0", "entryScene": "scenes/a.scene.json",
		"assets": []string{"assets/scripts/a.ts", "assets/textures/a.png", "assets/sounds/a.wav", "assets/ui/a.ui.html"},
	}
	data, _ := json.Marshal(man)
	writeFile(t, filepath.Join(buildDir, ".jm.json"), data)
	writeScript(t, buildDir, "assets/scripts/a", []byte("WASM"))
	writeFile(t, filepath.Join(buildDir, "scenes/a.scene.json"), []byte(`{"entities":[]}`))
	writeFile(t, filepath.Join(buildDir, "assets/textures/a.png"), []byte("PNG"))
	writeFile(t, filepath.Join(buildDir, "assets/sounds/a.wav"), []byte("WAV"))
	writeFile(t, filepath.Join(buildDir, "assets/ui/a.ui.html"), []byte("<div></div>"))
	writeFile(t, filepath.Join(buildDir, "assets/ui/theme.css"), []byte("p{}"))
	writeFile(t, filepath.Join(buildDir, "assets/maps/a.tmj"), []byte(`{}`))

	out := filepath.Join(tmp, "server.jm")
	if err := packArchive(buildDir, out, packOptions{server: true}); err != nil {
		t.Fatalf("pack: %v", err)
	}
	arc := readArchive(t, out)
	for _, kept := range []string{".jm.json", "assets/scripts/a.ts", "scenes/a.scene.json", "assets/maps/a.tmj"} {
		if _, err := arc.Read(kept); err != nil {
			t.Errorf("%s should be in a server archive: %v", kept, err)
		}
	}
	for _, dropped := range []string{"assets/textures/a.png", "assets/sounds/a.wav", "assets/ui/a.ui.html", "assets/ui/theme.css"} {
		if _, err := arc.Read(dropped); err == nil {
			t.Errorf("%s shouldn't be in a server archive", dropped)
		}
	}
	raw, _ := arc.Read(".jm.json")
	var packed struct {
		Assets []string `json:"assets"`
	}
	if err := json.Unmarshal(raw, &packed); err != nil {
		t.Fatal(err)
	}
	if !slices.Equal(packed.Assets, []string{"assets/scripts/a.ts"}) {
		t.Errorf("the preload list keeps only what's packed, got %v", packed.Assets)
	}
}

func TestPeerEnvGivesEachPeerItsOwnPlaces(t *testing.T) {
	t.Setenv("JM_CAPTURE_DIR", "frames")
	t.Setenv("JM_NET_TRACE", "out/net.jsonl")
	t.Setenv("JM_ERRORS", "-")
	t.Setenv("JM_SAVE_DIR", "saves")
	t.Setenv("JM_INPUT_REPLAY", "replays/{peer}.txt")
	t.Setenv("JM_NET_JOIN", "elsewhere:1")
	t.Setenv("JM_EXIT_AFTER_FRAMES", "600")

	get := func(env []string, key string) (string, bool) {
		for _, kv := range env {
			if k, v, _ := strings.Cut(kv, "="); k == key {
				return v, true
			}
		}
		return "", false
	}
	game := peerEnv(2, "peer2", []string{"JM_NET_JOIN=127.0.0.1:7777"})
	for key, want := range map[string]string{
		"JM_CAPTURE_DIR":       filepath.Join("frames", "peer2"),
		"JM_NET_TRACE":         "out/net.peer2.jsonl",
		"JM_ERRORS":            "-",
		"JM_SAVE_DIR":          filepath.Join("saves", "peer2"),
		"JM_INPUT_REPLAY":      "replays/peer2.txt",
		"JM_NET_JOIN":          "127.0.0.1:7777",
		"JM_EXIT_AFTER_FRAMES": "600",
	} {
		if got, _ := get(game, key); got != want {
			t.Errorf("%s = %q, want %q", key, got, want)
		}
	}

	server := peerEnv(0, "server", nil)
	for _, key := range []string{"JM_INPUT_REPLAY", "JM_EXIT_AFTER_FRAMES", "JM_NET_JOIN"} {
		if _, ok := get(server, key); ok {
			t.Errorf("the server shouldn't get %s", key)
		}
	}
}
