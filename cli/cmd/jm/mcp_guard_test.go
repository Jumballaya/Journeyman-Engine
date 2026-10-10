package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

// Resources are the project's own files: not through a link out of it, not
// in its hidden or build folders.
func TestMCPResourcesStayInTheProject(t *testing.T) {
	outside := t.TempDir()
	os.WriteFile(filepath.Join(outside, "secret.txt"), []byte("SECRET"), 0o644)
	t.Chdir(t.TempDir())
	os.WriteFile(".jm.json", []byte(`{}`), 0o644)
	os.Symlink(filepath.Join(outside, "secret.txt"), "link.txt")
	os.MkdirAll(".git", 0o755)
	os.WriteFile(".git/config.txt", []byte("token=abc"), 0o644)
	here, _ := filepath.Abs(".")
	for _, uri := range []string{"link.txt", ".git/config.txt", "../" + filepath.Base(outside) + "/secret.txt"} {
		if text, _, err := readResource("file://" + filepath.Join(here, uri)); err == nil {
			t.Errorf("%s was read: %q", uri, text)
		}
	}
	if _, _, err := readResource("file://" + filepath.Join(here, ".jm.json")); err != nil {
		t.Errorf("the manifest isn't readable: %v", err)
	}
}

// A tool's arguments are never read as jm's flags, and paths stay in the project.
func TestMCPToolArgumentsArentFlagsOrOutsidePaths(t *testing.T) {
	outside := t.TempDir()
	os.WriteFile(filepath.Join(outside, "o.json"), []byte(`{}`), 0o644)
	t.Chdir(t.TempDir())
	var ran []string
	saved := runJM
	runJM = func(args ...string) (string, bool) { ran = append(ran, strings.Join(args, " ")); return "", false }
	defer func() { runJM = saved }()
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"schema","arguments":{"component":"--engine=/bin/echo"}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"fmt","arguments":{"files":["`+filepath.Join(outside, "o.json")+`"]}}}`,
		`{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"test","arguments":{"specs":["../x.ts"]}}}`)
	if len(ran) != 1 || ran[0] != "schema -- --engine=/bin/echo" {
		t.Errorf("ran %q", ran)
	}
	for _, id := range []float64{2, 3} {
		if result := replies[id]["result"].(map[string]any); result["isError"] != true {
			t.Errorf("call %v outside the project ran: %v", id, result)
		}
	}
}

func TestDriveRefusesWhatTheGameWouldntAnswerOrShouldntDo(t *testing.T) {
	s := newMCPServer()
	fakeDriver(s, t.TempDir(), func(string) string { return `{"ok":true}` })
	for _, command := range []string{"", "   ", "# a note", "capture /etc/passwd"} {
		if reply, failed := s.driveCommand(command); !failed {
			t.Errorf("%q was sent: %s", command, reply)
		}
	}
	if s.game == nil {
		t.Error("a refused command stopped the game")
	}
}

// A game that never answers is stopped, not waited on forever.
func TestAStuckGameTimesOut(t *testing.T) {
	saved := replyTimeout
	replyTimeout = 50 * time.Millisecond
	defer func() { replyTimeout = saved }()
	s := newMCPServer()
	fakeDriver(s, t.TempDir(), func(string) string { time.Sleep(time.Second); return `{"ok":true}` })
	start := time.Now()
	if _, err := s.game.send("step 1"); err == nil || time.Since(start) > 900*time.Millisecond {
		t.Errorf("err=%v after %s", err, time.Since(start))
	}
}

// A replay the client sends (a result with an id, no method) gets no answer.
func TestMCPIgnoresTheClientsReplies(t *testing.T) {
	if _, answered := newMCPServer().respond([]byte(`{"jsonrpc":"2.0","id":5,"result":{}}`)); answered {
		t.Error("answered a reply")
	}
}

// Tool calls take turns: HTTP brings them on goroutines, and they share the game.
func TestMCPToolCallsTakeTurns(t *testing.T) {
	s := newMCPServer()
	var running, overlapped atomic.Int32
	s.tools = append(s.tools, mcpTool{Name: "slow", run: func(toolArgs) toolResult {
		if running.Add(1) > 1 {
			overlapped.Add(1)
		}
		time.Sleep(10 * time.Millisecond)
		running.Add(-1)
		return textResult("ok", false)
	}})
	var wg sync.WaitGroup
	for i := range 8 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			call, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": i + 1, "method": "tools/call", "params": map[string]any{"name": "slow"}})
			s.respond(call)
		}()
	}
	wg.Wait()
	if overlapped.Load() > 0 {
		t.Errorf("%d calls ran at once with another", overlapped.Load())
	}
}

func TestDriveStartFromAPlayTakesNothingElse(t *testing.T) {
	t.Chdir(t.TempDir())
	os.WriteFile(".jm.json", []byte(`{}`), 0o644)
	os.MkdirAll("build", 0o755)
	os.WriteFile("build/.jm.json", []byte(`{}`), 0o644)
	r := newMCPServer().startDriver(toolArgs{"play": "latest", "scene": "scenes/x.scene.json"})
	if !r.Failed || !strings.Contains(r.Text, "brings its own") {
		t.Errorf("drive_start: %s", r.Text)
	}
}
