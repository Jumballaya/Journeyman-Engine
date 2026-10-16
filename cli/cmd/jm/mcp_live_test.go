package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// play_live writes each command to the live game's inbox and reads its answer file.
func TestPlayLiveAsksTheRunningGame(t *testing.T) {
	root := sceneGame(t)
	t.Chdir(root)
	if r := sceneCall(t, `{"name":"play_live","arguments":{"command":"pause"}}`); r["isError"] != true {
		t.Errorf("no game running: %v", r)
	}
	dir := filepath.Join(liveDir(root), "77")
	os.MkdirAll(filepath.Join(dir, "answers"), 0o755)
	os.WriteFile(filepath.Join(dir, "in"), nil, 0o644)
	os.WriteFile(filepath.Join(dir, "status.json"), []byte(fmt.Sprintf(`{"frame":9,"scene":"s","paused":false,"speed":1,"updated":%d}`, time.Now().Unix())), 0o644)
	done := make(chan struct{})
	defer close(done)
	go func() { // the game: answers each request with its command
		read := 0
		for {
			select {
			case <-done:
				return
			case <-time.After(5 * time.Millisecond):
			}
			data, _ := os.ReadFile(filepath.Join(dir, "in"))
			lines := bufio.NewScanner(strings.NewReader(string(data[read:])))
			for lines.Scan() {
				read += len(lines.Bytes()) + 1
				var req struct{ ID, Command string }
				json.Unmarshal(lines.Bytes(), &req)
				answer, _ := json.Marshal(map[string]any{"ok": true, "echo": req.Command})
				os.WriteFile(filepath.Join(dir, "answers", req.ID+".json"), answer, 0o644)
			}
		}
	}()
	r := sceneCall(t, `{"name":"play_live","arguments":{"commands":["pause","get session.score"]}}`)
	if r["isError"] == true {
		t.Fatalf("play_live: %v", r)
	}
	answers := r["structuredContent"].(map[string]any)["answers"].([]any)
	if len(answers) != 2 || answers[1].(map[string]any)["echo"] != "get session.score" {
		t.Errorf("answers: %v", answers)
	}
	if left, _ := os.ReadDir(filepath.Join(dir, "answers")); len(left) != 0 {
		t.Errorf("answers read are removed: %d left", len(left))
	}
}
