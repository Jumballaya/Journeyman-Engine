package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"
)

// notifySession is a stdio session with a server that has the tools extra
// too. call sends a tools/call (meta: its _meta) and gives the notifications
// before its answer, a line each ("method params"); send writes any message.
func notifySession(t *testing.T, extra ...mcpTool) (call func(id int, tool, meta string) []string, send func(string), next func() map[string]any) {
	t.Helper()
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`})
	s := newMCPServer()
	s.tools = append(s.tools, extra...)
	inR, inW := io.Pipe()
	outR, outW := io.Pipe()
	go func() { s.serve(inR, outW); outW.Close() }()
	t.Cleanup(func() { inW.Close(); io.Copy(io.Discard, outR) })
	messages := make(chan map[string]any, 100)
	go func() {
		for lines := bufio.NewScanner(outR); lines.Scan(); {
			var msg map[string]any
			json.Unmarshal(lines.Bytes(), &msg)
			messages <- msg
		}
	}()
	send = func(line string) { io.WriteString(inW, line+"\n") }
	next = func() map[string]any {
		select {
		case msg := <-messages:
			return msg
		case <-time.After(10 * time.Second):
			panic("the server said nothing")
		}
	}
	call = func(id int, tool, meta string) []string {
		send(fmt.Sprintf(`{"jsonrpc":"2.0","id":%d,"method":"tools/call","params":{"name":%q,"arguments":{},"_meta":{%s}}}`, id, tool, meta))
		var got []string
		for msg := next(); msg["id"] != float64(id); msg = next() {
			got = append(got, fmt.Sprint(msg["method"], " ", jsonOf(msg["params"])))
		}
		return got
	}
	return call, send, next
}

// Progress goes only to a call that asked for it, with its token, each step further than the last.
func TestProgressIsSentOnlyWithAToken(t *testing.T) {
	steps := mcpTool{Name: "steps", InputSchema: object(map[string]any{}), run: func(a toolArgs) toolResult {
		a.call().progress(0, 0, "Building scripts")
		a.call().progress(1, 2, "not further than the last: not sent")
		a.call().progress(600, 1500, "Frame 600 of 1500")
		return textResult("done", false)
	}}
	call, _, _ := notifySession(t, steps)
	if got := call(1, "steps", ""); len(got) != 0 {
		t.Fatalf("no token, no progress: %v", got)
	}
	got := strings.Join(call(2, "steps", `"progressToken":"t2"`), "\n")
	if want := `notifications/progress {"message":"Building scripts","progress":1,"progressToken":"t2"}
notifications/progress {"message":"Frame 600 of 1500","progress":600,"progressToken":"t2","total":1500}`; got != want {
		t.Fatalf("progress:\n%s\nwant:\n%s", got, want)
	}
}

// A cancel reaches the call while it runs: its child and the child's own
// (sleep) are killed, the call ends at once and nothing answers it. The server answers on.
func TestACancelledCallStopsItsChildAndGetsNoAnswer(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("sh is Unix's")
	}
	pidFile := filepath.Join(t.TempDir(), "pid")
	ended := make(chan error, 1)
	slow := mcpTool{Name: "slow", InputSchema: object(map[string]any{}), run: func(a toolArgs) toolResult {
		_, err := a.call().run(exec.CommandContext(a.call().context(), "sh", "-c", `sleep 30 & echo $! > "$0"; wait`, pidFile))
		ended <- err
		return textResult("ran to the end", err != nil)
	}}
	_, send, next := notifySession(t, slow)
	send(`{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"slow","arguments":{}}}`)
	pid := 0
	for deadline := time.Now().Add(10 * time.Second); pid == 0 && time.Now().Before(deadline); time.Sleep(10 * time.Millisecond) {
		data, _ := os.ReadFile(pidFile)
		pid, _ = strconv.Atoi(strings.TrimSpace(string(data)))
	}
	send(`{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":7,"reason":"changed my mind"}}`)
	select {
	case err := <-ended:
		if !errors.Is(err, errCancelledByClient) {
			t.Errorf("the run's error: %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("the call ran on after its cancel")
	}
	sleep, _ := os.FindProcess(pid)
	for deadline := time.Now().Add(3 * time.Second); pid == 0 || sleep.Signal(syscall.Signal(0)) == nil; time.Sleep(20 * time.Millisecond) {
		if time.Now().After(deadline) {
			t.Fatalf("the child's sleep (%d) lives on", pid)
		}
	}
	send(`{"jsonrpc":"2.0","id":8,"method":"ping"}`)
	if msg := next(); msg["id"] != 8.0 {
		t.Fatalf("a cancelled call gets no answer, the next does: %v", msg)
	}
}

// A call cancelled while it waits its turn never runs.
func TestACallCancelledBeforeItsTurnDoesntRun(t *testing.T) {
	s, ran := newMCPServer(), false
	s.tools = append(s.tools, mcpTool{Name: "mark", run: func(toolArgs) toolResult { ran = true; return textResult("", false) }})
	call := []byte(`{"jsonrpc":"2.0","id":"a","method":"tools/call","params":{"name":"mark","arguments":{}}}`)
	s.control(call) // read off the wire: queued behind a running tool
	s.control([]byte(`{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"\u0061"}}`))
	if _, answered := s.respond(call); answered || ran {
		t.Fatalf("answered %v, ran %v", answered, ran)
	}
}

// Log messages go out at the client's level and above: warning until it sets one.
func TestSetLevelFiltersLogMessages(t *testing.T) {
	logs := mcpTool{Name: "logs", InputSchema: object(map[string]any{}), run: func(a toolArgs) toolResult {
		a.call().log("info", "engine", "loaded level 1")
		a.call().log("warning", "engine", "texture missing")
		a.call().log("error", "jm", "script failed")
		return textResult("", false)
	}}
	call, send, next := notifySession(t, logs)
	levels := func(id int) string {
		got := ""
		for _, line := range call(id, "logs", "") {
			_, level, _ := strings.Cut(line, `"level":"`)
			got += " " + level[:strings.IndexByte(level, '"')]
		}
		return strings.TrimSpace(got)
	}
	if got := levels(1); got != "warning error" {
		t.Errorf("by default: %s", got)
	}
	for i, c := range [][2]string{{"error", "error"}, {"debug", "info warning error"}, {"loud", "info warning error"}} {
		send(fmt.Sprintf(`{"jsonrpc":"2.0","id":%d,"method":"logging/setLevel","params":{"level":%q}}`, 10+i, c[0]))
		if reply := next(); (reply["error"] != nil) != (c[0] == "loud") {
			t.Fatalf("setLevel %s: %v", c[0], reply)
		}
		if got := levels(20 + i); got != c[1] {
			t.Errorf("at %s: %s, want %s", c[0], got, c[1])
		}
	}
}

// What the engine logs while a call runs, and a jm child's stages and
// warnings, reach the client; the stage lines stay out of the tool's output.
func TestEngineLogAndChildOutputReachTheClient(t *testing.T) {
	output := ""
	run := mcpTool{Name: "run", InputSchema: object(map[string]any{}), run: func(a toolArgs) toolResult {
		os.MkdirAll(filepath.Join("build", "logs"), 0o755)
		os.WriteFile(filepath.Join("build", "logs", "engine.log"), []byte("[2026-10-17 09:00:00.000] [engine] [warning] no music\n"), 0o644)
		out, _ := a.call().run(exec.CommandContext(a.call().context(), "sh", "-c",
			`echo "jm-progress: Building" >&2; echo '{"level":"warning","message":"unused prefab"}'; echo done`))
		output = string(out)
		return textResult("", false)
	}}
	call, _, _ := notifySession(t, run)
	got := strings.Join(call(1, "run", `"progressToken":5`), "\n")
	for _, want := range []string{`notifications/progress {"message":"Building","progress":1,"progressToken":5}`,
		`notifications/message {"data":{"level":"warning","message":"unused prefab"},"level":"warning","logger":"jm"}`,
		`notifications/message {"data":"no music","level":"warning","logger":"engine"}`} {
		if !strings.Contains(got, want) {
			t.Errorf("missing %s in:\n%s", want, got)
		}
	}
	if strings.Contains(output, "jm-progress") || !strings.Contains(output, "done") {
		t.Errorf("the tool's output: %q", output)
	}
}
