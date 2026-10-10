package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"image"
	"image/png"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// mcpSession sends requests (one JSON-RPC message per line) and returns the
// replies by id.
func mcpSession(t *testing.T, requests ...string) map[float64]map[string]any {
	t.Helper()
	var out bytes.Buffer
	if err := serveMCP(strings.NewReader(strings.Join(requests, "\n")), &out); err != nil {
		t.Fatal(err)
	}
	replies := map[float64]map[string]any{}
	for _, line := range strings.Split(strings.TrimSpace(out.String()), "\n") {
		var msg map[string]any
		if err := json.Unmarshal([]byte(line), &msg); err != nil {
			t.Fatalf("not JSON: %s", line)
		}
		id, _ := msg["id"].(float64)
		replies[id] = msg
	}
	return replies
}

func TestMCPInitializesAndListsTools(t *testing.T) {
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"0"}}}`,
		`{"jsonrpc":"2.0","method":"notifications/initialized"}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/list"}`,
		`{"jsonrpc":"2.0","id":3,"method":"nope"}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"drive","arguments":{"command":"step"}}}`)
	if len(replies) != 4 {
		t.Fatalf("a notification gets no reply: got %d replies", len(replies))
	}
	info := replies[1]["result"].(map[string]any)
	if info["protocolVersion"] != mcpProtocolVersion || info["serverInfo"].(map[string]any)["name"] != "journeyman" {
		t.Fatalf("initialize: %v", info)
	}
	names := []string{}
	for _, tool := range replies[2]["result"].(map[string]any)["tools"].([]any) {
		names = append(names, tool.(map[string]any)["name"].(string))
	}
	if strings.Join(names, ",") != "build,doctor,test,golden,schema,generate,fmt,export,drive_start,drive,drive_frame,drive_stop,session,plays_list,play_show,play_frame,play_state,play_verify,play_resume" {
		t.Fatalf("tools: %v", names)
	}
	if replies[3]["error"].(map[string]any)["code"].(float64) != -32601 {
		t.Fatalf("an unknown method is an error: %v", replies[3])
	}
	result := replies[4]["result"].(map[string]any)
	if result["isError"] != true || !strings.Contains(result["content"].([]any)[0].(map[string]any)["text"].(string), "drive_start") {
		t.Fatalf("driving with no game running should say to start one: %v", result)
	}
}

// Resources are the project's files (not build/ or node_modules), read only inside the project.
func TestMCPResourcesAreTheProjectsFiles(t *testing.T) {
	dir := t.TempDir()
	for path, text := range map[string]string{
		".jm.json": "{}", "scenes/main.scene.json": "{}", "assets/scripts/hero.ts": "// hero",
		"build/scenes/main.scene.json": "{}", "assets/scripts/node_modules/x/index.ts": "", "assets/hero.png": "",
	} {
		full := filepath.Join(dir, path)
		if err := os.MkdirAll(filepath.Dir(full), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(text), 0644); err != nil {
			t.Fatal(err)
		}
	}
	t.Chdir(dir)
	names := []string{}
	for _, r := range projectResources() {
		if name := r["name"].(string); !strings.HasPrefix(name, "docs/") {
			names = append(names, name)
		}
	}
	if strings.Join(names, ",") != "schema,.jm.json,assets/scripts/hero.ts,scenes/main.scene.json" {
		t.Fatalf("resources: %v", names)
	}
	abs, _ := filepath.Abs("assets/scripts/hero.ts")
	if text, mime, err := readResource("file://" + filepath.ToSlash(abs)); err != nil || text != "// hero" || mime != "text/x-typescript" {
		t.Fatalf("read: %q %q %v", text, mime, err)
	}
	if text, mime, err := readResource("jm://docs/agents"); err != nil || !strings.Contains(text, "jm build --json") || mime != "text/markdown" {
		t.Fatalf("docs: %q %v", mime, err)
	}
	if _, _, err := readResource("file:///etc/hosts"); err == nil {
		t.Fatal("files outside the project can't be read")
	}
}

// fakeDriver gives s a game whose engine answers each command line with
// answer(command), its folder work.
func fakeDriver(s *mcpServer, work string, answer func(string) string) {
	cmdR, cmdW := io.Pipe()
	replyR, replyW := io.Pipe()
	go func() {
		lines := bufio.NewScanner(cmdR)
		for lines.Scan() {
			fmt.Fprintln(replyW, answer(lines.Text()))
		}
		replyW.Close()
	}()
	s.game = &drivenGame{cmd: &exec.Cmd{}, in: cmdW, out: bufio.NewScanner(replyR), work: work}
}

func TestADriveBatchCantFloodTheContext(t *testing.T) {
	s := newMCPServer()
	big := `{"ok":true,"state":"` + strings.Repeat("x", 400) + `"}`
	fakeDriver(s, t.TempDir(), func(c string) string {
		if c == "boom" {
			return `{"ok":false,"error":"no such command"}`
		}
		return big
	})
	out, failed := s.driveBatch([]string{"state"}, 100, 2000)
	lines := strings.Split(out, "\n")
	if failed || lines[len(lines)-1] != `{"truncated":true,"after":5}` || len(lines) != 6 {
		t.Errorf("past the limit: failed=%v, %d lines ending %s", failed, len(lines), lines[len(lines)-1])
	}
	out, failed = s.driveBatch([]string{"step 1", "boom", "step 1"}, 3, 1<<20)
	if !failed || strings.Count(out, "\n") != 1 {
		t.Errorf("a failure should stop the batch with the replies so far: failed=%v\n%s", failed, out)
	}
}

func TestDriveFrameShowsTheGameWithNoFileToManage(t *testing.T) {
	s := newMCPServer()
	work := filepath.Join(t.TempDir(), "game")
	os.Mkdir(work, 0o755)
	fakeDriver(s, work, func(c string) string {
		if path, ok := strings.CutPrefix(c, "capture "); ok {
			f, _ := os.Create(path)
			png.Encode(f, image.NewRGBA(image.Rect(0, 0, 64, 36)))
			f.Close()
			return `{"ok":true,"path":"` + path + `"}`
		}
		return `{"ok":true,"frame":43}`
	})
	s.driveCommand("step 43")
	r := s.driveFrame(nil)
	if r.Failed || len(r.Images) != 1 || r.Images[0].MimeType != "image/jpeg" || !strings.Contains(r.Text, "Frame 42") {
		t.Fatalf("drive_frame: %+v", r.Text)
	}
	s.stopDriver()
	if _, err := os.Stat(work); !os.IsNotExist(err) {
		t.Error("the driver's folder outlived it")
	}

	fakeDriver(s, t.TempDir(), func(string) string {
		return `{"ok":false,"error":"no pixels with JM_RENDERER=none (state has the draw list)"}`
	})
	if r := s.driveFrame(nil); !r.Failed || !strings.Contains(r.Text, "gl: true") {
		t.Errorf("without GL: %s", r.Text)
	}
}

func TestDriveStopNamesThePlayItRecorded(t *testing.T) {
	s := newMCPServer()
	fakeDriver(s, t.TempDir(), func(string) string { return `{"ok":true}` })
	s.game.play = filepath.Join(t.TempDir(), ".jm", "plays", "2000-01-01_000000")
	var stop mcpTool
	for _, tool := range s.tools {
		if tool.Name == "drive_stop" {
			stop = tool
		}
	}
	r := stop.run(toolArgs{})
	if !strings.Contains(r.Text, `"play":"2000-01-01_000000"`) || s.game != nil {
		t.Errorf("drive_stop: %s", r.Text)
	}
}

func TestDriveNeedsACommand(t *testing.T) {
	s := newMCPServer()
	for _, tool := range s.tools {
		if tool.Name == "drive" {
			if r := tool.run(toolArgs{}); !r.Failed || !strings.Contains(r.Text, "give a command") {
				t.Errorf("drive with nothing: %s", r.Text)
			}
		}
	}
}

func TestMCPFmtAndExportRunJM(t *testing.T) {
	var ran []string
	saved := runJM
	runJM = func(args ...string) (string, bool) { ran = append(ran, strings.Join(args, " ")); return "", false }
	defer func() { runJM = saved }()
	mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"fmt","arguments":{}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"fmt","arguments":{"check":true,"files":["scenes/a.scene.json"]}}}`,
		`{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"export","arguments":{}}}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"export","arguments":{"target":"windows-amd64","server":true}}}`)
	want := "fmt|fmt --check scenes/a.scene.json|export|export --target windows-amd64 --server"
	if got := strings.Join(ran, "|"); got != want {
		t.Errorf("ran %q; want %q", got, want)
	}
}

func TestABuildIsStaleWhenMissingOrOlderThanItsSources(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{}`), 0o644)
	os.MkdirAll(filepath.Join(root, "assets", "scripts", "node_modules"), 0o755)
	if !buildIsStale(root) {
		t.Error("no build isn't stale")
	}
	os.MkdirAll(filepath.Join(root, "build"), 0o755)
	os.WriteFile(filepath.Join(root, "build", ".jm.json"), []byte(`{}`), 0o644)
	old := time.Now().Add(-time.Hour)
	os.Chtimes(filepath.Join(root, ".jm.json"), old, old)
	if buildIsStale(root) {
		t.Error("a build newer than its sources is stale")
	}
	os.WriteFile(filepath.Join(root, "assets", "scripts", "node_modules", "x.js"), nil, 0o644)
	ahead := time.Now().Add(time.Minute)
	os.Chtimes(filepath.Join(root, "assets", "scripts", "node_modules", "x.js"), ahead, ahead)
	if buildIsStale(root) {
		t.Error("node_modules counts as a source")
	}
	os.WriteFile(filepath.Join(root, "assets", "player.ts"), nil, 0o644)
	later := time.Now().Add(time.Minute) // past the build's time, however coarse the clock
	os.Chtimes(filepath.Join(root, "assets", "player.ts"), later, later)
	if !buildIsStale(root) {
		t.Error("an edited script doesn't make the build stale")
	}
}
