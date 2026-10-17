package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"math/big"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// A tool call's side channel to the client, over stdio: progress (when the call has a progressToken),
// log messages (the engine's log and jm's warnings, at the client's level) and its cancellation.
// Over HTTP each call is one answer, so they're dropped there.

// logLevels are MCP's (RFC 5424's), least severe first.
var logLevels = []string{"debug", "info", "notice", "warning", "error", "critical", "alert", "emergency"}

// defaultLogLevel keeps a client that never sets one from getting every info line.
const defaultLogLevel = 3 // warning

// errCancelledByClient is a call's context's cause when notifications/cancelled stopped it.
var errCancelledByClient = errors.New("the client cancelled the call")

// inflight is the tool calls not answered yet, by request id: a cancel finds
// its call there, even one still waiting its turn.
type inflight struct {
	mu    sync.Mutex
	calls map[string]runningCall
}

type runningCall struct {
	ctx    context.Context
	cancel context.CancelCauseFunc
}

func requestKey(id json.RawMessage) string {
	if s := ""; json.Unmarshal(id, &s) == nil { // "\u0061" and "a": the same request
		key, _ := json.Marshal(s)
		return string(key)
	}
	if n, ok := new(big.Rat).SetString(strings.TrimSpace(string(id))); ok {
		return n.RatString() // 1e3 and 1000, exactly: 2^53+1 isn't 2^53
	}
	return string(id)
}

// begin is the context of the call id names, made when it's first seen.
func (f *inflight) begin(id json.RawMessage) context.Context {
	f.mu.Lock()
	defer f.mu.Unlock()
	key := requestKey(id)
	if call, ok := f.calls[key]; ok {
		return call.ctx
	}
	ctx, cancel := context.WithCancelCause(context.Background())
	f.calls[key] = runningCall{ctx, cancel}
	return ctx
}

// cancel stops the call id names (one already answered, or unknown, is left
// be); nil: every call, the server is stopping.
func (f *inflight) cancel(id json.RawMessage) {
	f.mu.Lock()
	defer f.mu.Unlock()
	for key, call := range f.calls {
		if id == nil || key == requestKey(id) {
			call.cancel(errCancelledByClient)
		}
	}
}

// end forgets the call, answered.
func (f *inflight) end(id json.RawMessage) {
	f.mu.Lock()
	defer f.mu.Unlock()
	if call, ok := f.calls[requestKey(id)]; ok {
		call.cancel(nil)
		delete(f.calls, requestKey(id))
	}
}

// control handles, as it's read, a message that mustn't wait behind a
// running tool: a cancel. True: data was one, handled.
func (s *mcpServer) control(data []byte) bool {
	var msg rpcMessage
	if json.Unmarshal(data, &msg) != nil {
		return false
	}
	switch {
	case msg.Method == "notifications/cancelled":
		var p struct {
			RequestID json.RawMessage `json:"requestId"`
		}
		if json.Unmarshal(msg.Params, &p) == nil && len(p.RequestID) > 0 && string(p.RequestID) != "null" {
			s.inflight.cancel(p.RequestID)
		}
		return true
	case msg.Method == "tools/call" && msg.ID != nil:
		s.inflight.begin(msg.ID) // now: a cancel may come before its turn
	}
	return false
}

func (s *mcpServer) setLogLevel(params json.RawMessage) (any, *rpcError) {
	var p struct {
		Level string `json:"level"`
	}
	_ = json.Unmarshal(params, &p)
	i := slices.Index(logLevels, p.Level)
	if i < 0 {
		return nil, &rpcError{Code: -32602, Message: "level is one of " + strings.Join(logLevels, ", ")}
	}
	s.logLevel.Store(int32(i))
	return map[string]any{}, nil
}

// toolCall is one tools/call as it runs: its context (cancelled when the client cancels it) and where
// its progress and log messages go. A nil *toolCall is the CLI's: never cancelled, telling no one.
type toolCall struct {
	ctx   context.Context
	token json.RawMessage // the client's progressToken: nil, no progress
	peer  *mcpPeer        // nil over HTTP
	level *atomic.Int32
	mu    sync.Mutex
	done  float64 // the last progress sent: each must be more
}

func (s *mcpServer) newCall(ctx context.Context, params json.RawMessage) *toolCall {
	var p struct {
		Meta struct {
			Token json.RawMessage `json:"progressToken"`
		} `json:"_meta"`
	}
	_ = json.Unmarshal(params, &p)
	c := &toolCall{ctx: ctx, peer: s.peer, level: &s.logLevel}
	if t := bytes.TrimSpace(p.Meta.Token); len(t) > 0 && (t[0] == '"' || t[0] == '-' || t[0] >= '0' && t[0] <= '9') {
		c.token = t // a string or a number: anything else isn't a token
	}
	return c
}

const callKey = "jm/call" // where a call travels in its tool's arguments: JSON can't put a *toolCall there

// call is the call these arguments came with (nil outside one).
func (a toolArgs) call() *toolCall {
	c, _ := a[callKey].(*toolCall)
	return c
}

func (c *toolCall) context() context.Context {
	if c == nil {
		return context.Background()
	}
	return c.ctx
}

// progress reports done of total (0: not known; done 0: one step more than
// the last), when the call asked for progress and it's further than the last.
func (c *toolCall) progress(done, total float64, message string) {
	if c == nil || c.token == nil || c.peer == nil {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if done == 0 {
		done = c.done + 1
	}
	if done <= c.done || c.ctx.Err() != nil {
		return
	}
	c.done = done
	params := map[string]any{"progressToken": c.token, "progress": done, "message": message}
	if total > 0 {
		params["total"] = total
	}
	c.notify("notifications/progress", params)
}

// log sends a log message when level is at least the client's.
func (c *toolCall) log(level, logger string, data any) {
	if c == nil || c.peer == nil || slices.Index(logLevels, level) < int(c.level.Load()) {
		return
	}
	c.notify("notifications/message", map[string]any{"level": level, "logger": logger, "data": data})
}

func (c *toolCall) notify(method string, params any) {
	data, _ := json.Marshal(params)
	c.peer.send(rpcMessage{JSONRPC: "2.0", Method: method, Params: data})
}

// progressPrefix marks a jm child's stage line on stderr (JM_PROGRESS, stage).
const progressPrefix = "jm-progress: "

// run runs cmd (made with the call's context) for the call: its whole tree
// killed when the call is cancelled, a jm child's stages sent as progress
// and its warnings and errors as log messages. The rest of its output,
// stdout and stderr together, comes back.
func (c *toolCall) run(cmd *exec.Cmd) ([]byte, error) {
	if os.Getenv("JM_PROGRESS") == "" { // else this jm is in such a tree already: its own group would escape the kill
		killableTree(cmd)
	}
	cmd.WaitDelay = 5 * time.Second // a grandchild holding the output open doesn't hold this up
	if cmd.Env == nil {
		cmd.Env = os.Environ()
	}
	cmd.Env = append(cmd.Env, "JM_PROGRESS=1")
	w := &childOutput{call: c}
	cmd.Stdout, cmd.Stderr = w, w // the same writer: exec writes it from one goroutine at a time
	err := cmd.Run()
	w.flush()
	return w.kept.Bytes(), err
}

// childOutput splits a child's output into lines: stages become progress,
// problems log messages too, and the rest is kept.
type childOutput struct {
	call    *toolCall
	partial []byte
	kept    bytes.Buffer
}

func (w *childOutput) Write(p []byte) (int, error) {
	w.partial = append(w.partial, p...)
	for {
		i := bytes.IndexByte(w.partial, '\n')
		if i < 0 {
			return len(p), nil
		}
		w.line(w.partial[:i+1])
		w.partial = w.partial[i+1:]
	}
}

func (w *childOutput) flush() {
	if len(w.partial) > 0 {
		w.line(w.partial)
		w.partial = nil
	}
}

func (w *childOutput) line(raw []byte) {
	text := strings.TrimSpace(string(raw))
	if stage, ok := strings.CutPrefix(text, progressPrefix); ok {
		w.call.progress(0, 0, stage)
		return
	}
	w.kept.Write(raw)
	var d Diagnostic
	switch {
	case strings.HasPrefix(text, "{") && json.Unmarshal([]byte(text), &d) == nil && (d.Level == "warning" || d.Level == "error"):
		w.call.log(d.Level, "jm", d)
	case strings.HasPrefix(text, "warning: "):
		w.call.log("warning", "jm", strings.TrimPrefix(text, "warning: "))
	}
}

// followEngineLog sends the lines the engine logs in the game at root while
// the call runs (to the log game_log reads), until the returned stop (which
// sends the last of them).
func (c *toolCall) followEngineLog(root string) (stop func()) {
	if c == nil || c.peer == nil || root == "" {
		return func() {}
	}
	read := map[string]int64{} // how far each log has been sent: what earlier runs wrote isn't this call's
	for _, log := range []string{filepath.Join(root, "logs", "engine.log"), filepath.Join(root, "build", "logs", "engine.log")} {
		if real, err := realPath(log); err == nil {
			if info, err := os.Stat(real); err == nil {
				read[real] = info.Size()
			}
		}
	}
	sendNew := func() {
		if log := latestLog(root); log != "" { // never a pipe, or a link out of the game
			read[log] = c.sendLogFrom(log, read[log])
		}
	}
	return every(250*time.Millisecond, sendNew)
}

// every calls tick each period, and once more at the returned stop.
func every(period time.Duration, tick func()) (stop func()) {
	quit, finished := make(chan struct{}), make(chan struct{})
	go func() {
		defer close(finished)
		ticker := time.NewTicker(period)
		defer ticker.Stop()
		for {
			select {
			case <-quit:
				tick()
				return
			case <-ticker.C:
				tick()
			}
		}
	}()
	return func() { close(quit); <-finished }
}

// sendLogFrom sends log's whole lines past offset, giving where they end.
func (c *toolCall) sendLogFrom(log string, offset int64) int64 {
	file, err := os.Open(log)
	if err != nil {
		return offset
	}
	defer file.Close()
	if info, err := file.Stat(); err != nil || info.Size() < offset {
		offset = 0 // started over
	}
	if _, err := file.Seek(offset, io.SeekStart); err != nil {
		return offset
	}
	data, _ := io.ReadAll(io.LimitReader(file, 4<<20))
	end := bytes.LastIndexByte(data, '\n')
	if end < 0 {
		return offset
	}
	for _, raw := range strings.Split(string(data[:end]), "\n") {
		level, text := "info", raw
		if m := logFormat.FindStringSubmatch(raw); m != nil {
			level, text = m[2], m[3]
		}
		if level == "trace" {
			level = "debug"
		}
		if slices.Contains(logLevels, level) {
			c.log(level, "engine", text)
		}
	}
	return offset + int64(end) + 1
}
