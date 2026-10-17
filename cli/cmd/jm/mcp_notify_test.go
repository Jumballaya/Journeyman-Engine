package main

import (
	"bufio"
	"encoding/json"
	"errors"
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
// too: send writes a message, next reads the server's next one.
func notifySession(t *testing.T, extra ...mcpTool) (send func(string), next func() map[string]any) {
	t.Helper()
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`})
	s := newMCPServer()
	s.tools = append(s.tools, extra...)
	inR, inW := io.Pipe()
	outR, outW := io.Pipe()
	stopped := make(chan struct{})
	go func() { s.serve(inR, outW); outW.Close(); close(stopped) }()
	t.Cleanup(func() { inW.Close(); io.Copy(io.Discard, outR); <-stopped })
	messages := make(chan map[string]any, 100)
	go func() {
		lines := bufio.NewScanner(outR)
		lines.Buffer(make([]byte, 1<<20), 1<<24)
		for lines.Scan() {
			var msg map[string]any
			json.Unmarshal(lines.Bytes(), &msg)
			messages <- msg
		}
		close(messages)
	}()
	send = func(line string) { io.WriteString(inW, line+"\n") }
	next = func() map[string]any {
		t.Helper()
		select {
		case msg, ok := <-messages:
			if !ok {
				t.Fatal("the server stopped")
			}
			return msg
		case <-time.After(10 * time.Second):
			t.Fatal("the server said nothing")
		}
		return nil
	}
	return send, next
}

// Progress goes only to a call that asked for it, with its token, each step further than the last.
func TestProgressIsSentOnlyWithAToken(t *testing.T) {
	steps := mcpTool{Name: "steps", InputSchema: object(map[string]any{}), run: func(a toolArgs) toolResult {
		a.call().step("Building scripts")
		a.call().progress(1, 2, "too late: not further") // not more than the last
		a.call().progress(600, 1500, "Frame 600 of 1500")
		return textResult("done", false)
	}}
	send, next := notifySession(t, steps)
	send(`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"steps","arguments":{}}}`)
	if msg := next(); msg["id"] != 1.0 {
		t.Fatalf("no token, no progress: %v", msg)
	}
	send(`{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"steps","arguments":{},"_meta":{"progressToken":"t2"}}}`)
	var got []string
	for {
		msg := next()
		if msg["id"] == 2.0 {
			break
		}
		if msg["method"] != "notifications/progress" {
			t.Fatalf("not progress: %v", msg)
		}
		p := msg["params"].(map[string]any)
		if p["progressToken"] != "t2" {
			t.Fatalf("progress for another token: %v", p)
		}
		got = append(got, jsonOf(p))
	}
	want := []string{`{"message":"Building scripts","progress":1,"progressToken":"t2"}`,
		`{"message":"Frame 600 of 1500","progress":600,"progressToken":"t2","total":1500}`}
	if strings.Join(got, "\n") != strings.Join(want, "\n") {
		t.Fatalf("progress:\n%s\nwant:\n%s", strings.Join(got, "\n"), strings.Join(want, "\n"))
	}
}

// A cancel reaches the call while it runs: its child is killed, the call
// ends at once, and nothing answers it. The server answers on after.
func TestACancelledCallStopsItsChildAndGetsNoAnswer(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("sleep and kill(pid, 0) are Unix's")
	}
	pidFile := filepath.Join(t.TempDir(), "pid")
	ended := make(chan error, 1)
	slow := mcpTool{Name: "slow", InputSchema: object(map[string]any{}), run: func(a toolArgs) toolResult {
		cmd := exec.CommandContext(a.call().context(), "sh", "-c", `echo $$ > "$0"; sleep 30; echo late`, pidFile)
		_, err := a.call().run(cmd)
		ended <- err
		return textResult("ran to the end", err != nil)
	}}
	send, next := notifySession(t, slow)
	send(`{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"slow","arguments":{}}}`)
	var pid int
	for deadline := time.Now().Add(10 * time.Second); pid == 0 && time.Now().Before(deadline); time.Sleep(10 * time.Millisecond) {
		data, _ := os.ReadFile(pidFile)
		pid, _ = strconv.Atoi(strings.TrimSpace(string(data)))
	}
	if pid == 0 {
		t.Fatal("the tool's child never started")
	}
	at := time.Now()
	send(`{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":7,"reason":"the person changed their mind"}}`)
	select {
	case err := <-ended:
		if !errors.Is(err, errCancelledByClient) {
			t.Errorf("the run's error: %v", err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("the call ran on after its cancel")
	}
	if took := time.Since(at); took > 5*time.Second {
		t.Errorf("stopping took %s", took)
	}
	if err := syscall.Kill(-pid, 0); !errors.Is(err, syscall.ESRCH) {
		t.Errorf("the child's process group lives on: %v", err)
	}
	send(`{"jsonrpc":"2.0","id":8,"method":"ping"}`)
	if msg := next(); msg["id"] != 8.0 {
		t.Fatalf("a cancelled call gets no answer, the next does: %v", msg)
	}
}

// A call cancelled while it waits its turn never runs.
func TestACallCancelledBeforeItsTurnDoesntRun(t *testing.T) {
	ran := false
	tool := mcpTool{Name: "mark", InputSchema: object(map[string]any{}), run: func(toolArgs) toolResult { ran = true; return textResult("", false) }}
	s := newMCPServer()
	s.tools = append(s.tools, tool)
	call := []byte(`{"jsonrpc":"2.0","id":"a","method":"tools/call","params":{"name":"mark","arguments":{}}}`)
	s.control(call) // read off the wire: queued behind a running tool
	s.control([]byte(`{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"a"}}`))
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
	send, next := notifySession(t, logs)
	heard := func(id float64) []string {
		send(`{"jsonrpc":"2.0","id":` + jsonOf(id) + `,"method":"tools/call","params":{"name":"logs","arguments":{}}}`)
		var got []string
		for msg := next(); msg["id"] != id; msg = next() {
			p := msg["params"].(map[string]any)
			got = append(got, p["level"].(string)+" "+p["logger"].(string)+" "+p["data"].(string))
		}
		return got
	}
	if got := strings.Join(heard(1), ", "); got != "warning engine texture missing, error jm script failed" {
		t.Errorf("by default: %s", got)
	}
	send(`{"jsonrpc":"2.0","id":2,"method":"logging/setLevel","params":{"level":"error"}}`)
	next()
	if got := strings.Join(heard(3), ", "); got != "error jm script failed" {
		t.Errorf("at error: %s", got)
	}
	send(`{"jsonrpc":"2.0","id":4,"method":"logging/setLevel","params":{"level":"debug"}}`)
	next()
	if got := len(heard(5)); got != 3 {
		t.Errorf("at debug, all 3: %d", got)
	}
	send(`{"jsonrpc":"2.0","id":6,"method":"logging/setLevel","params":{"level":"loud"}}`)
	if msg := next(); msg["error"] == nil {
		t.Errorf("no such level: %v", msg)
	}
}

// What the engine logs while a call runs, and a jm child's stages and
// warnings, reach the client; the stage lines stay out of the tool's output.
func TestEngineLogAndChildOutputReachTheClient(t *testing.T) {
	var output string
	run := mcpTool{Name: "run", InputSchema: object(map[string]any{}), run: func(a toolArgs) toolResult {
		os.MkdirAll(filepath.Join("build", "logs"), 0o755)
		os.WriteFile(filepath.Join("build", "logs", "engine.log"), []byte("[2026-10-17 09:00:00.000] [engine] [warning] no music\n"), 0o644)
		cmd := exec.CommandContext(a.call().context(), "sh", "-c",
			`echo "jm-progress: Building" >&2; echo '{"level":"warning","category":"content","message":"unused prefab"}'; echo done`)
		out, _ := a.call().run(cmd)
		output = string(out)
		return textResult("", false)
	}}
	send, next := notifySession(t, run)
	send(`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"run","arguments":{},"_meta":{"progressToken":5}}}`)
	var got []string
	for msg := next(); msg["id"] != 1.0; msg = next() {
		got = append(got, msg["method"].(string)+" "+jsonOf(msg["params"]))
	}
	for _, want := range []string{`notifications/progress {"message":"Building","progress":1,"progressToken":5}`,
		`notifications/message {"data":{"category":"content","level":"warning","message":"unused prefab"},"level":"warning","logger":"jm"}`,
		`notifications/message {"data":"no music","level":"warning","logger":"engine"}`} {
		if !strings.Contains(strings.Join(got, "\n"), want) {
			t.Errorf("missing %s in:\n%s", want, strings.Join(got, "\n"))
		}
	}
	if strings.Contains(output, "jm-progress") || !strings.Contains(output, "done") {
		t.Errorf("the tool's output: %q", output)
	}
}
