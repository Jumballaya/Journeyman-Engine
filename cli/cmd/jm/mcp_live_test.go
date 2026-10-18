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

// Of two live runs, the later started is the one played now, whichever wrote its status last; run names the other.
func TestTheLiveRunIsTheLatestStarted(t *testing.T) {
	root := t.TempDir()
	status := func(name string, started int64) {
		dir := filepath.Join(liveDir(root), name)
		os.MkdirAll(dir, 0o755)
		os.WriteFile(filepath.Join(dir, "status.json"), []byte(fmt.Sprintf(`{"frame":1,"updated":%d,"started":%d}`, time.Now().Unix(), started)), 0o644)
	}
	status("old", 1000)
	status("new", 2000)
	for _, last := range []string{"old", "new", "old"} { // heartbeats in either order
		status(last, map[string]int64{"old": 1000, "new": 2000}[last])
		if got := filepath.Base(liveGame(root, "")); got != "new" {
			t.Errorf("after %s's heartbeat: %s", last, got)
		}
	}
	if got := filepath.Base(liveGame(root, "old")); got != "old" {
		t.Errorf("run old: %s", got)
	}
	if got := liveGame(root, "gone"); got != "" {
		t.Errorf("a run not live: %s", got)
	}
}

// play_live steers the game named, whichever is open, and says whose run answered.
func TestPlayLiveSteersTheGameNamed(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"a", "b"} {
		os.MkdirAll(filepath.Join(home, g), 0o755)
		os.WriteFile(filepath.Join(home, g, ".jm.json"), []byte(`{"name":"`+g+`"}`), 0o644)
	}
	dir := filepath.Join(liveDir(filepath.Join(home, "a")), "7")
	os.MkdirAll(filepath.Join(dir, "answers"), 0o755)
	os.WriteFile(filepath.Join(dir, "in"), nil, 0o644)
	os.WriteFile(filepath.Join(dir, "status.json"), []byte(fmt.Sprintf(`{"updated":%d,"started":1}`, time.Now().Unix())), 0o644)
	t.Chdir(filepath.Join(home, "b"))
	go func() { // a's game: answers its first request
		for i := 0; i < 400; i++ {
			time.Sleep(5 * time.Millisecond)
			data, _ := os.ReadFile(filepath.Join(dir, "in"))
			var req struct{ ID string }
			if json.Unmarshal([]byte(strings.SplitN(string(data), "\n", 2)[0]), &req) == nil && req.ID != "" {
				os.WriteFile(filepath.Join(dir, "answers", req.ID+".json"), []byte(`{"ok":true}`), 0o644)
				return
			}
		}
	}()
	r := sceneCall(t, `{"name":"play_live","arguments":{"command":"status","game":"a"}}`)
	s, _ := r["structuredContent"].(map[string]any)
	want, _ := filepath.EvalSymlinks(filepath.Join(home, "a"))
	if r["isError"] == true || s["game"] != want || s["run"] != "7" {
		t.Errorf("play_live: %v", r)
	}
}

// A frame the game never sends: cancelling the call stops the wait, and the next request is answered at once.
func TestACancelledLiveFrameStopsWaiting(t *testing.T) {
	_, send, next := notifySession(t)
	root, _ := os.Getwd()
	dir := filepath.Join(liveDir(root), "77")
	os.MkdirAll(filepath.Join(dir, "answers"), 0o755)
	os.WriteFile(filepath.Join(dir, "in"), nil, 0o644)
	os.WriteFile(filepath.Join(dir, "status.json"), []byte(fmt.Sprintf(`{"frame":9,"scene":"s","paused":false,"speed":1,"updated":%d}`, time.Now().Unix())), 0o644)
	done := make(chan struct{})
	defer close(done)
	capturing := make(chan struct{})
	go func() { // the game: answers each request but the capture
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
				if strings.HasPrefix(req.Command, "capture ") {
					close(capturing)
					continue
				}
				answer, _ := json.Marshal(map[string]any{"ok": true})
				os.WriteFile(filepath.Join(dir, "answers", req.ID+".json"), answer, 0o644)
			}
		}
	}()
	send(`{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"play_live","arguments":{"command":"pause","frame":true}}}`)
	select {
	case <-capturing:
	case <-time.After(5 * time.Second):
		t.Fatal("no capture asked for")
	}
	send(`{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":9}}`)
	asked := time.Now()
	send(`{"jsonrpc":"2.0","id":10,"method":"ping"}`)
	if msg := next(); msg["id"] != 10.0 {
		t.Fatalf("a cancelled call gets no answer, the next does: %v", msg)
	}
	if waited := time.Since(asked); waited > time.Second {
		t.Errorf("the ping waited %v for the cancelled capture", waited)
	}
}
