package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"

	"github.com/spf13/cobra"
)

// jm mcp: the CLI as an MCP server on stdio, for agents that speak MCP. A
// thin wrapper by design: each tool runs a jm command (or talks to the engine's
// stepped driver), so there's nothing MCP can do that jm can't, and one source
// of truth. Resources are the project's files and the engine's schema.

var mcpCmd = &cobra.Command{
	Use:   "mcp",
	Short: "Serve this CLI over MCP (stdio), for agents",
	Long: `Speaks the Model Context Protocol on stdin/stdout (JSON-RPC, a message
per line), from the project in the current folder.

Tools run jm's own commands: build, test, golden, schema, generate, and
drive_start / drive / drive_stop for the engine's stepped driver (JM_DRIVE:
one game at a time, a command per call, e.g. "step 60", "press Enter",
"state"). Resources are the project's files (.jm.json, scenes, prefabs,
scripts, UI, data) and jm://schema.

Register it with an MCP client as the command "jm mcp", run in the project.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		return serveMCP(os.Stdin, os.Stdout)
	},
}

const mcpProtocolVersion = "2025-06-18"

type rpcMessage struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method,omitempty"`
	Params  json.RawMessage `json:"params,omitempty"`
	Result  any             `json:"result,omitempty"`
	Error   *rpcError       `json:"error,omitempty"`
}

type rpcError struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}

type mcpTool struct {
	Name        string                                   `json:"name"`
	Description string                                   `json:"description"`
	InputSchema map[string]any                           `json:"inputSchema"`
	run         func(args map[string]any) (string, bool) // output, failed
}

func serveMCP(in io.Reader, out io.Writer) error {
	server := &mcpServer{out: out}
	server.tools = server.makeTools()
	defer server.stopDriver()
	scanner := bufio.NewScanner(in)
	scanner.Buffer(make([]byte, 1<<20), 64<<20)
	for scanner.Scan() {
		line := bytes.TrimSpace(scanner.Bytes())
		if len(line) == 0 {
			continue
		}
		var msg rpcMessage
		if err := json.Unmarshal(line, &msg); err != nil {
			server.send(rpcMessage{JSONRPC: "2.0", ID: json.RawMessage("null"), Error: &rpcError{-32700, "parse error: " + err.Error()}})
			continue
		}
		if msg.ID == nil {
			continue // a notification (initialized, cancelled): nothing to answer
		}
		result, err := server.handle(msg.Method, msg.Params)
		reply := rpcMessage{JSONRPC: "2.0", ID: msg.ID}
		if err != nil {
			reply.Error = err
		} else {
			reply.Result = result
		}
		server.send(reply)
	}
	return scanner.Err()
}

type mcpServer struct {
	out   io.Writer
	mu    sync.Mutex
	tools []mcpTool

	driver *exec.Cmd // the stepped driver, while one runs
	stdin  io.WriteCloser
	lines  *bufio.Scanner
}

func (s *mcpServer) send(msg rpcMessage) {
	s.mu.Lock()
	defer s.mu.Unlock()
	data, _ := json.Marshal(msg)
	fmt.Fprintf(s.out, "%s\n", data)
}

func (s *mcpServer) handle(method string, params json.RawMessage) (any, *rpcError) {
	switch method {
	case "initialize":
		return map[string]any{
			"protocolVersion": mcpProtocolVersion,
			"capabilities":    map[string]any{"tools": map[string]any{}, "resources": map[string]any{}},
			"serverInfo":      map[string]any{"name": "journeyman", "version": version},
			"instructions": "Journeyman builds 2D games from files: scenes and prefabs (JSON), AssemblyScript scripts, " +
				"HTML/CSS UI. Edit the project's files directly; use these tools to build, test and play it. " +
				"Read jm://schema for every component's keys.",
		}, nil
	case "ping":
		return map[string]any{}, nil
	case "tools/list":
		return map[string]any{"tools": s.tools}, nil
	case "tools/call":
		var call struct {
			Name      string         `json:"name"`
			Arguments map[string]any `json:"arguments"`
		}
		if err := json.Unmarshal(params, &call); err != nil {
			return nil, &rpcError{-32602, "invalid params: " + err.Error()}
		}
		for _, tool := range s.tools {
			if tool.Name == call.Name {
				output, failed := tool.run(call.Arguments)
				return map[string]any{"content": []map[string]any{{"type": "text", "text": output}}, "isError": failed}, nil
			}
		}
		return nil, &rpcError{-32602, "unknown tool " + call.Name}
	case "resources/list":
		return map[string]any{"resources": projectResources()}, nil
	case "resources/read":
		var read struct {
			URI string `json:"uri"`
		}
		if err := json.Unmarshal(params, &read); err != nil {
			return nil, &rpcError{-32602, "invalid params: " + err.Error()}
		}
		text, mime, err := readResource(read.URI)
		if err != nil {
			return nil, &rpcError{-32002, err.Error()}
		}
		return map[string]any{"contents": []map[string]any{{"uri": read.URI, "mimeType": mime, "text": text}}}, nil
	}
	return nil, &rpcError{-32601, "method not found: " + method}
}

func object(properties map[string]any, required ...string) map[string]any {
	schema := map[string]any{"type": "object", "properties": properties}
	if len(required) > 0 {
		schema["required"] = required
	}
	return schema
}

func (s *mcpServer) makeTools() []mcpTool {
	str := func(description string) map[string]any {
		return map[string]any{"type": "string", "description": description}
	}
	return []mcpTool{
		{Name: "build", Description: "jm build --json: compile scripts, bake atlases, check scenes and prefabs. JSON lines; the last is the result.",
			InputSchema: object(map[string]any{}), run: func(map[string]any) (string, bool) { return runJM("build", "--json") }},
		{Name: "test", Description: "jm test: run tests/*.spec.ts (game logic, no engine).",
			InputSchema: object(map[string]any{"specs": map[string]any{"type": "array", "items": map[string]any{"type": "string"}, "description": "spec files (default: all)"}}),
			run: func(a map[string]any) (string, bool) {
				return runJM(append([]string{"test"}, stringList(a["specs"])...)...)
			}},
		{Name: "golden", Description: "jm golden --json: compare frames with tests/golden images (update: record them instead). Build first.",
			InputSchema: object(map[string]any{"names": map[string]any{"type": "array", "items": map[string]any{"type": "string"}}, "update": map[string]any{"type": "boolean"}}),
			run: func(a map[string]any) (string, bool) {
				args := []string{"golden", "--json"}
				if update, _ := a["update"].(bool); update {
					args = append(args, "--update")
				}
				return runJM(append(args, stringList(a["names"])...)...)
			}},
		{Name: "schema", Description: "jm schema: every component's scene keys and script fields (or one component's), as JSON.",
			InputSchema: object(map[string]any{"component": str("e.g. SpriteComponent (default: all)")}),
			run: func(a map[string]any) (string, bool) {
				if c, _ := a["component"].(string); c != "" {
					return runJM("schema", c)
				}
				return runJM("schema")
			}},
		{Name: "generate", Description: "jm generate <kind> <name>: make a file from a template (jm generate list shows the kinds).",
			InputSchema: object(map[string]any{"kind": str("e.g. prefab, script, scene, ui, shader, bindings, list"), "name": str("the new file's name")}, "kind"),
			run: func(a map[string]any) (string, bool) {
				args := []string{"generate", fmt.Sprint(a["kind"])}
				if name, _ := a["name"].(string); name != "" {
					args = append(args, name)
				}
				return runJM(args...)
			}},
		{Name: "drive_start", Description: "Start the built game under the stepped driver (headless; no window or GL unless gl is true). " +
			"It waits at frame 0 until told to step. One game at a time; starting again restarts it.",
			InputSchema: object(map[string]any{
				"scene":   str("start in this scene instead of the entry scene"),
				"session": map[string]any{"type": "object", "description": "game state set before the first frame (a deep link)"},
				"gl":      map[string]any{"type": "boolean", "description": "render with OpenGL, so capture works (needs a display)"},
			}),
			run: s.startDriver},
		{Name: "drive", Description: "One driver command, answered as JSON: step [n], state, down|up|press <Key>, " +
			"set <key> <json>, scene <path>, capture <path> (with gl), quit.",
			InputSchema: object(map[string]any{"command": str("e.g. \"step 60\", \"press Enter\", \"state\"")}, "command"),
			run:         func(a map[string]any) (string, bool) { return s.driveCommand(fmt.Sprint(a["command"])) }},
		{Name: "drive_stop", Description: "Stop the driven game.", InputSchema: object(map[string]any{}),
			run: func(map[string]any) (string, bool) { s.stopDriver(); return `{"ok":true}`, false }},
	}
}

func stringList(v any) []string {
	list, _ := v.([]any)
	out := []string{}
	for _, item := range list {
		out = append(out, fmt.Sprint(item))
	}
	return out
}

// runJM runs this jm with args in the current folder: its output, and whether it failed.
func runJM(args ...string) (string, bool) {
	self, err := os.Executable()
	if err != nil {
		return err.Error(), true
	}
	out, err := exec.Command(self, args...).CombinedOutput()
	text := strings.TrimSpace(string(out))
	if err != nil && text == "" {
		text = err.Error()
	}
	return text, err != nil
}

func (s *mcpServer) startDriver(a map[string]any) (string, bool) {
	s.stopDriver()
	manifestPath := filepath.Join("build", archive.ManifestEntryKey)
	man, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return "no build to run (build first): " + err.Error(), true
	}
	engine, err := resolveEnginePath(man.EnginePath, manifestPath)
	if err == nil {
		engine, err = filepath.Abs(engine)
	}
	if err != nil {
		return err.Error(), true
	}
	work, err := os.MkdirTemp("", "jm-drive-")
	if err != nil {
		return err.Error(), true
	}
	env := append(os.Environ(), "JM_DRIVE=1", "JM_HEADLESS=1", "JM_SAVE_DIR="+filepath.Join(work, "save"))
	if gl, _ := a["gl"].(bool); !gl {
		env = append(env, "JM_RENDERER=none")
	}
	if scene, _ := a["scene"].(string); scene != "" {
		env = append(env, "JM_ENTRY_SCENE="+scene)
	}
	if session, ok := a["session"].(map[string]any); ok && len(session) > 0 {
		data, _ := json.Marshal(session)
		path := filepath.Join(work, "session.json")
		if err := os.WriteFile(path, data, 0644); err != nil {
			return err.Error(), true
		}
		env = append(env, "JM_SESSION="+path)
	}
	cmd := exec.Command(engine, ".")
	cmd.Dir, cmd.Env = "build", env
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return err.Error(), true
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err.Error(), true
	}
	if err := cmd.Start(); err != nil {
		return err.Error(), true
	}
	s.driver, s.stdin = cmd, stdin
	s.lines = bufio.NewScanner(stdout)
	s.lines.Buffer(make([]byte, 1<<20), 256<<20)
	if !s.lines.Scan() { // {"ready": true, ...}
		s.stopDriver()
		return "the game didn't start (see the build's logs/engine.log)", true
	}
	return s.lines.Text(), false
}

func (s *mcpServer) driveCommand(command string) (string, bool) {
	if s.driver == nil {
		return "no game running: call drive_start first", true
	}
	if strings.ContainsAny(command, "\n\r") {
		return "one command per call", true
	}
	if _, err := fmt.Fprintln(s.stdin, command); err != nil || !s.lines.Scan() {
		s.stopDriver()
		return "the game ended", true
	}
	reply := s.lines.Text()
	var parsed struct {
		OK bool `json:"ok"`
	}
	_ = json.Unmarshal([]byte(reply), &parsed)
	if strings.TrimSpace(command) == "quit" {
		s.stopDriver()
	}
	return reply, !parsed.OK
}

func (s *mcpServer) stopDriver() {
	if s.driver == nil {
		return
	}
	_ = s.stdin.Close() // end of input ends the driver
	_ = s.driver.Wait()
	s.driver, s.stdin, s.lines = nil, nil, nil
}

// projectResources lists the files an agent reads and writes, and the schema.
func projectResources() []map[string]any {
	resources := []map[string]any{{"uri": "jm://schema", "name": "schema", "mimeType": "application/json",
		"description": "every component's scene keys and script fields (jm schema)"}}
	var files []string
	_ = filepath.WalkDir(".", func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() {
			if name := d.Name(); path != "." && (strings.HasPrefix(name, ".") || name == "build" || name == "node_modules" || name == "dist") {
				return filepath.SkipDir
			}
			return nil
		}
		if mimeOf(path) != "" {
			files = append(files, filepath.ToSlash(path))
		}
		return nil
	})
	sort.Strings(files)
	for _, f := range files {
		abs, _ := filepath.Abs(f)
		resources = append(resources, map[string]any{"uri": "file://" + filepath.ToSlash(abs), "name": f, "mimeType": mimeOf(f)})
	}
	return resources
}

func mimeOf(path string) string {
	switch {
	case strings.HasSuffix(path, ".json"), strings.HasSuffix(path, ".tmj"), strings.HasSuffix(path, ".tsj"):
		return "application/json"
	case strings.HasSuffix(path, ".ts"):
		return "text/x-typescript"
	case strings.HasSuffix(path, ".html"):
		return "text/html"
	case strings.HasSuffix(path, ".css"):
		return "text/css"
	case strings.HasSuffix(path, ".frag"), strings.HasSuffix(path, ".txt"), strings.HasSuffix(path, ".md"):
		return "text/plain"
	}
	return ""
}

func readResource(uri string) (string, string, error) {
	if uri == "jm://schema" {
		out, failed := runJM("schema")
		if failed {
			return "", "", fmt.Errorf("%s", out)
		}
		return out, "application/json", nil
	}
	path, ok := strings.CutPrefix(uri, "file://")
	if !ok {
		return "", "", fmt.Errorf("unknown resource %s", uri)
	}
	root, _ := filepath.Abs(".")
	if rel, err := filepath.Rel(root, path); err != nil || strings.HasPrefix(rel, "..") {
		return "", "", fmt.Errorf("%s is outside the project", uri)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return "", "", err
	}
	return string(data), mimeOf(path), nil
}
