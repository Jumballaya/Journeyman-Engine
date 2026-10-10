package main

import (
	"bufio"
	"bytes"
	"encoding/base64"
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
	"github.com/Jumballaya/Journeyman-Engine/internal/docs"
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
		if mcpDir != "" {
			if err := os.Chdir(mcpDir); err != nil {
				return fmt.Errorf("--dir: %w", err)
			}
		}
		if mcpHTTP != "" {
			return serveMCPHTTP(mcpHTTP, mcpAllowOrigins)
		}
		return serveMCP(os.Stdin, os.Stdout)
	},
}

var mcpHTTP, mcpDir string
var mcpAllowOrigins []string

func init() {
	mcpCmd.Flags().StringVar(&mcpDir, "dir", "", "the game's folder (default: the current one), for clients that start servers elsewhere")
	mcpCmd.Flags().StringVar(&mcpHTTP, "http", "", `serve over HTTP at this address instead (e.g. "127.0.0.1:8787"): for ChatGPT apps; prints the URL to use`)
	mcpCmd.Flags().StringArrayVar(&mcpAllowOrigins, "allow-origin", nil, "with --http, also answer requests from this origin host (default: only this machine's)")
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
	Name        string         `json:"name"`
	Title       string         `json:"title,omitempty"`
	Description string         `json:"description"`
	InputSchema map[string]any `json:"inputSchema"`
	// readOnlyHint etc.: hosts skip confirming tools that only look.
	Annotations map[string]any `json:"annotations,omitempty"`
	// Host extras: a ChatGPT app's widget (openai/outputTemplate) and its kin.
	Meta map[string]any `json:"_meta,omitempty"`
	run  func(args map[string]any) toolResult
}

// toolResult is a tool's answer: text (and images) for the model, structured
// data for hosts that show it, and _meta only a widget sees (thumbnails as
// data URIs: kept out of the model's context).
type toolResult struct {
	Text       string
	Images     []mcpImage
	Structured any
	Meta       map[string]any
	Failed     bool
}

type mcpImage struct {
	Data     []byte
	MimeType string
}

func textResult(text string, failed bool) toolResult { return toolResult{Text: text, Failed: failed} }

// textTool adapts a command that answers in text.
func textTool(run func(map[string]any) (string, bool)) func(map[string]any) toolResult {
	return func(a map[string]any) toolResult { return textResult(run(a)) }
}

func (r toolResult) reply() map[string]any {
	content := []map[string]any{{"type": "text", "text": r.Text}}
	for _, img := range r.Images {
		content = append(content, map[string]any{"type": "image", "data": base64.StdEncoding.EncodeToString(img.Data), "mimeType": img.MimeType})
	}
	out := map[string]any{"content": content, "isError": r.Failed}
	if r.Structured != nil {
		out["structuredContent"] = r.Structured
	}
	if r.Meta != nil {
		out["_meta"] = r.Meta
	}
	return out
}

func newMCPServer(out io.Writer) *mcpServer {
	server := &mcpServer{out: out}
	server.tools = append(server.makeTools(), server.playTools()...)
	return server
}

func serveMCP(in io.Reader, out io.Writer) error {
	server := newMCPServer(out)
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
	work   string // its save and frames, removed when it stops
	frames int    // frames seen with drive_frame, naming their files
	play   string // the play it's recording, if it is
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
				"Read jm://docs/agents first (the workflow), jm://schema for every component's keys, and jm://docs/scripting for the script API. " +
				"The person plays the game and every play is recorded, with F8 markers at moments they want you to see: when they talk about " +
				"something that happened while playing, call play_show (and play_frame / play_state at the moment) before guessing; after a fix, " +
				"play_verify says whether their play now goes differently, and play_resume lets them try it right there.",
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
				if call.Arguments == nil {
					call.Arguments = map[string]any{}
				}
				return tool.run(call.Arguments).reply(), nil
			}
		}
		return nil, &rpcError{-32602, "unknown tool " + call.Name}
	case "resources/list":
		return map[string]any{"resources": append([]map[string]any{playsWidgetResource()}, projectResources()...)}, nil
	case "resources/read":
		var read struct {
			URI string `json:"uri"`
		}
		if err := json.Unmarshal(params, &read); err != nil {
			return nil, &rpcError{-32602, "invalid params: " + err.Error()}
		}
		if read.URI == playsWidgetURI {
			w := playsWidgetResource()
			return map[string]any{"contents": []map[string]any{{"uri": read.URI, "mimeType": w["mimeType"], "text": strings.ReplaceAll(playsWidget, "__JM_VERSION__", version), "_meta": w["_meta"]}}}, nil
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
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": false, "openWorldHint": false}, InputSchema: object(map[string]any{}), run: textTool(func(map[string]any) (string, bool) { return runJM("build", "--json") })},
		{Name: "doctor", Description: "jm doctor --json: jm's and the engine's versions, the script toolchain (Node, AssemblyScript), the project, and any problems with their fixes.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}), run: textTool(func(map[string]any) (string, bool) { return runJM("doctor", "--json") })},
		{Name: "test", Description: "jm test --json: run tests/*.spec.ts (game logic, no engine). A JSON line per test; the last is the result.",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": false, "openWorldHint": false}, InputSchema: object(map[string]any{"specs": map[string]any{"type": "array", "items": map[string]any{"type": "string"}, "description": "spec files (default: all)"}}),
			run: textTool(func(a map[string]any) (string, bool) {
				return runJM(append([]string{"test", "--json"}, stringList(a["specs"])...)...)
			})},
		{Name: "golden", Description: "jm golden --json: compare frames with tests/golden images (update: record them instead). Build first.",
			InputSchema: object(map[string]any{"names": map[string]any{"type": "array", "items": map[string]any{"type": "string"}}, "update": map[string]any{"type": "boolean"}}),
			run: textTool(func(a map[string]any) (string, bool) {
				args := []string{"golden", "--json"}
				if update, _ := a["update"].(bool); update {
					args = append(args, "--update")
				}
				return runJM(append(args, stringList(a["names"])...)...)
			})},
		{Name: "schema", Description: "jm schema: every component's scene keys and script fields (or one component's), as JSON.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"component": str("e.g. SpriteComponent (default: all)")}),
			run: textTool(func(a map[string]any) (string, bool) {
				if c, _ := a["component"].(string); c != "" {
					return runJM("schema", c)
				}
				return runJM("schema")
			})},
		{Name: "generate", Description: "jm generate <kind> <name>: make a file from a template (jm generate list shows the kinds).",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": false, "openWorldHint": false}, InputSchema: object(map[string]any{"kind": str("e.g. prefab, script, scene, ui, shader, bindings, list"), "name": str("the new file's name")}, "kind"),
			run: textTool(func(a map[string]any) (string, bool) {
				args := []string{"generate", fmt.Sprint(a["kind"])}
				if name, _ := a["name"].(string); name != "" {
					args = append(args, name)
				}
				return runJM(args...)
			})},
		{Name: "fmt", Description: "jm fmt: write the project's JSON (scenes, prefabs, data, the manifest) in the layout the editor writes, so diffs stay small. Run it before committing.",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": false, "openWorldHint": false},
			InputSchema: object(map[string]any{
				"files": map[string]any{"type": "array", "items": map[string]any{"type": "string"}, "description": "only these files (default: the whole project)"},
				"check": map[string]any{"type": "boolean", "description": "change nothing; fail if a file needs formatting"},
			}),
			run: textTool(func(a map[string]any) (string, bool) {
				args := []string{"fmt"}
				if check, _ := a["check"].(bool); check {
					args = append(args, "--check")
				}
				return runJM(append(args, stringList(a["files"])...)...)
			})},
		{Name: "export", Description: "jm export: build the game and write a standalone executable with everything inside (an .app on macOS) to dist/, for the person to run or share.",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": false, "openWorldHint": false},
			InputSchema: object(map[string]any{
				"target": str("platform as os-arch, e.g. windows-amd64 (default: this machine; others need that platform's engine as player)"),
				"server": map[string]any{"type": "boolean", "description": "export the dedicated multiplayer server instead"},
			}),
			run: textTool(func(a map[string]any) (string, bool) {
				args := []string{"export"}
				if target := argString(a, "target"); target != "" {
					args = append(args, "--target", target)
				}
				if server, _ := a["server"].(bool); server {
					args = append(args, "--server")
				}
				return runJM(args...)
			})},
		{Name: "drive_start", Description: "Start the built game under the stepped driver (headless; no window or GL unless gl is true). " +
			"It waits at frame 0 until told to step, or with play, at that moment of the person's recorded play. One game at a time; starting again restarts it. " +
			"With record, what you play is recorded as a play like the person's (drive_stop gives its id; play_show, play_frame and the timeline then work on it).",
			Annotations: readOnly(), InputSchema: object(map[string]any{
				"scene":   str("start in this scene instead of the entry scene"),
				"session": map[string]any{"type": "object", "description": "game state set before the first frame (a deep link)"},
				"gl":      map[string]any{"type": "boolean", "description": "render with OpenGL, so drive_frame can show the game (needs a display)"},
				"play":    str("start at a moment of a recorded play instead (plays_list): replayed exactly up to there, then yours to drive"),
				"at":      str("with play: the moment, e.g. \"marker:2\", \"12.5s\", a frame (default: its end)"),
				"record":  map[string]any{"type": "boolean", "description": "record this run as a play (drive_stop gives its id)"},
				"seed":    map[string]any{"type": "integer", "description": "the run's random seed (default: a new one; a play replays its own)"},
				"visible": map[string]any{"type": "boolean", "description": "show the game in a window as you drive it, so the person can watch (implies gl)"},
			}),
			run: textTool(s.startDriver)},
		{Name: "drive", Description: "Driver commands, each answered as JSON: step [n] [dt], state [part...] [tag=Name...] [Component...] (e.g. state session tag=Player), get [tag=Name] <path> (get tag=Ball TransformComponent.x), " +
			"down|up|press <Key>, move x y, click [x y], wheel dy, set <key> <json>, scene <path>, quit (drive_frame shows the game). " +
			"To follow something frame by frame, send commands with repeat instead of a call per frame: " +
			"commands [\"step 1\", \"get tag=Player TransformComponent.y\"], repeat 40 (a reply line each). " +
			"A failing command stops the batch: the commands before it already ran, and the result is an error holding the replies so far. " +
			"Past 1 MB of replies the batch stops too (the last line says {\"truncated\":true,\"after\":n commands}): ask for less, e.g. get instead of state.",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": false, "openWorldHint": false},
			InputSchema: object(map[string]any{
				"command":  str("e.g. \"step 60\", \"press Enter\", \"state\""),
				"commands": map[string]any{"type": "array", "items": map[string]any{"type": "string"}, "description": "several commands in order, instead of command"},
				"repeat":   map[string]any{"type": "integer", "description": "run commands this many times over (default 1, at most 1000)"},
			}),
			run: textTool(func(a map[string]any) (string, bool) {
				commands := stringList(a["commands"])
				if len(commands) == 0 {
					command, _ := a["command"].(string)
					return s.driveCommand(command)
				}
				repeat := 1
				if n, ok := a["repeat"].(float64); ok && n >= 1 {
					repeat = min(int(n), 1000)
				}
				return s.driveBatch(commands, repeat, driveReplyLimit)
			})},
		{Name: "drive_frame", Title: "See the driven game", Description: "An image of the driven game as it is now (the last frame it ran). " +
			"Needs a game started with gl: true. Use it to see what your commands did, e.g. after step 60 or a click.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}), run: s.driveFrame},
		{Name: "drive_stop", Description: "Stop the driven game.", Annotations: readOnly(), InputSchema: object(map[string]any{}),
			run: textTool(func(map[string]any) (string, bool) {
				play := s.play
				s.stopDriver()
				if play == "" {
					return `{"ok":true}`, false
				}
				// Its session.json is complete once the game has ended.
				reply, _ := json.Marshal(map[string]any{"ok": true, "play": filepath.Base(play),
					"next": "play_show shows it, as it does the person's plays"})
				return string(reply), false
			})},
		{Name: "session", Description: "Play a multiplayer session on this machine (jm run --peers), headless with no GL, " +
			"in real time: the game's server if it has one, and N games. Each game can replay its own input. " +
			"Returns each peer's state when it ended: its net section (role, player, players, shared entities), " +
			"scene, session store and shared entities' tags and fields. Build first.",
			Annotations: readOnly(), InputSchema: object(map[string]any{
				"peers":   map[string]any{"type": "integer", "description": "how many games (default 2)"},
				"frames":  map[string]any{"type": "integer", "description": "frames each game runs, 60 a second (default 300)"},
				"replays": map[string]any{"type": "array", "items": map[string]any{"type": "string"}, "description": "replay text per game, in order (\"30 down ArrowRight\" lines)"},
				"latency": map[string]any{"type": "integer", "description": "simulated latency per message, ms"},
				"loss":    map[string]any{"type": "number", "description": "simulated loss of unreliable messages, 0..1"},
			}),
			run: textTool(runSessionTool)},
	}
}

// runSessionTool is the "session" tool: jm run --peers with every peer's
// final state dumped, then those dumps, trimmed to what a session is about.
func runSessionTool(a map[string]any) (string, bool) {
	peers, frames := 2, 300
	if v, ok := a["peers"].(float64); ok && v >= 1 {
		peers = int(v)
	}
	if v, ok := a["frames"].(float64); ok && v >= 1 {
		frames = int(v)
	}
	work, err := os.MkdirTemp("", "jm-session-")
	if err != nil {
		return err.Error(), true
	}
	defer os.RemoveAll(work)
	replays := stringList(a["replays"])
	for i := 1; i <= peers && len(replays) > 0; i++ {
		text := "" // a peer past the list just plays nothing
		if i <= len(replays) {
			text = replays[i-1] + "\n"
		}
		if err := os.WriteFile(filepath.Join(work, fmt.Sprintf("peer%d.txt", i)), []byte(text), 0o644); err != nil {
			return err.Error(), true
		}
	}
	args := []string{"run", "--peers", fmt.Sprint(peers)}
	if v, ok := a["latency"].(float64); ok && v > 0 {
		args = append(args, "--latency", fmt.Sprint(int(v)))
	}
	if v, ok := a["loss"].(float64); ok && v > 0 {
		args = append(args, "--loss", fmt.Sprint(v))
	}
	self, err := os.Executable()
	if err != nil {
		return err.Error(), true
	}
	cmd := exec.Command(self, args...)
	cmd.Env = append(os.Environ(), "JM_RENDERER=none", "JM_REALTIME=1", fmt.Sprintf("JM_EXIT_AFTER_FRAMES=%d", frames),
		"JM_DUMP_DIR="+filepath.Join(work, "dump"), "JM_SAVE_DIR="+filepath.Join(work, "save"),
		"JM_ERRORS="+filepath.Join(work, "errors.jsonl"))
	if len(replays) > 0 {
		cmd.Env = append(cmd.Env, "JM_INPUT_REPLAY="+filepath.Join(work, "{peer}.txt"))
	}
	output, runErr := cmd.CombinedOutput()

	result := map[string]any{}
	for i := 1; i <= peers; i++ {
		label := fmt.Sprintf("peer%d", i)
		data, err := os.ReadFile(filepath.Join(work, "dump", label, "state_exit.json"))
		if err != nil {
			result[label] = map[string]any{"error": "no state: it didn't finish"}
			continue
		}
		var state map[string]any
		if json.Unmarshal(data, &state) != nil {
			continue
		}
		shared := []any{}
		entities, _ := state["entities"].([]any)
		for _, e := range entities {
			entity, _ := e.(map[string]any)
			components, _ := entity["components"].(map[string]any)
			if _, ok := components["NetworkComponent"]; ok {
				shared = append(shared, map[string]any{"tags": entity["tags"], "transform": components["TransformComponent"]})
			}
		}
		result[label] = map[string]any{"net": state["net"], "scene": state["scene"], "session": state["session"], "shared": shared}
	}
	errors := []string{}
	if matches, _ := filepath.Glob(filepath.Join(work, "errors*.jsonl")); len(matches) > 0 {
		for _, m := range matches {
			if data, _ := os.ReadFile(m); len(strings.TrimSpace(string(data))) > 0 {
				errors = append(errors, filepath.Base(m)+": "+strings.TrimSpace(string(data)))
			}
		}
	}
	result["errors"] = errors
	tail := string(output)
	if len(tail) > 3000 {
		tail = tail[len(tail)-3000:]
	}
	result["output"] = tail
	text, _ := json.MarshalIndent(result, "", "  ")
	return string(text), runErr != nil || len(errors) > 0
}

func stringList(v any) []string {
	list, _ := v.([]any)
	out := []string{}
	for _, item := range list {
		out = append(out, fmt.Sprint(item))
	}
	return out
}

// runJM runs this jm with args in the current folder: its output, and whether
// it failed. A variable so tests can see what a tool runs.
var runJM = func(args ...string) (string, bool) {
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
	if _, err := manifest.LoadManifest(manifestPath); err != nil {
		return "no build to run (build first): " + err.Error(), true
	}
	engine, err := resolveEnginePath()
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
	env := append(os.Environ(), "JM_DRIVE=1", "JM_SAVE_DIR="+filepath.Join(work, "save"))
	gl, _ := a["gl"].(bool)
	visible, _ := a["visible"].(bool)
	if !visible {
		env = append(env, "JM_HEADLESS=1")
	}
	if !gl && !visible {
		env = append(env, "JM_RENDERER=none")
	}
	if seed, ok := a["seed"].(float64); ok {
		env = append(env, fmt.Sprintf("JM_SEED=%d", uint64(seed)))
	}
	var play string
	if record, _ := a["record"].(bool); record {
		root, _ := os.Getwd()
		pruneOldPlays(root)
		play = newPlayDir(root)
		writePlayInfo(root, play)
		env = append(env, "JM_RECORD_DIR="+play)
	}
	if scene, _ := a["scene"].(string); scene != "" {
		env = append(env, "JM_ENTRY_SCENE="+scene)
	}
	var startFrame uint64
	if ref, ok := a["play"].(string); ok && ref != "" {
		_, p, f, err := momentOf(ref, argString(a, "at"))
		if err != nil {
			return err.Error(), true
		}
		startFrame = f + 1
		env = append(env, "JM_PLAY_SESSION="+p.Dir, fmt.Sprintf("JM_PLAY_UNTIL=%d", startFrame))
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
	s.driver, s.stdin, s.work, s.play = cmd, stdin, work, play
	s.lines = bufio.NewScanner(stdout)
	s.lines.Buffer(make([]byte, 1<<20), 256<<20)
	if !s.lines.Scan() { // {"ready": true, ...}
		s.stopDriver()
		return "the game didn't start (see the build's logs/engine.log)", true
	}
	if startFrame == 0 {
		return s.lines.Text(), false
	}
	// Replay the play up to the moment: the game is then where the person was.
	reply, failed := s.driveCommand(fmt.Sprintf("step %d", startFrame))
	if failed {
		return reply, true
	}
	return fmt.Sprintf(`{"ok":true,"ready":true,"frame":%d,"fromPlay":true,"step":%s}`, startFrame, reply), false
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

// driveReplyLimit keeps a batch's replies to what an agent's context can take.
const driveReplyLimit = 1 << 20

// driveBatch runs commands repeat times over, a reply line each. It stops at
// the first failure (the rest would run on from somewhere unexpected) and once
// the replies pass limit bytes, saying so in a last line.
func (s *mcpServer) driveBatch(commands []string, repeat, limit int) (string, bool) {
	var replies []string
	size, ran := 0, 0
	for range repeat {
		for _, c := range commands {
			if size > limit {
				replies = append(replies, fmt.Sprintf(`{"truncated":true,"after":%d}`, ran))
				return strings.Join(replies, "\n"), false
			}
			reply, failed := s.driveCommand(c)
			replies = append(replies, reply)
			size += len(reply) + 1
			ran++
			if failed {
				return strings.Join(replies, "\n"), true
			}
		}
	}
	return strings.Join(replies, "\n"), false
}

func (s *mcpServer) stopDriver() {
	if s.driver == nil {
		return
	}
	_ = s.stdin.Close() // end of input ends the driver
	_ = s.driver.Wait()
	if s.work != "" {
		_ = os.RemoveAll(s.work)
	}
	s.driver, s.stdin, s.lines, s.work, s.play = nil, nil, nil, "", ""
}

// driveFrame is an image of the driven game as it is now (the last frame
// run). The file is the driver's own, gone when it stops: the agent gets the
// image, not a path to manage.
func (s *mcpServer) driveFrame(map[string]any) toolResult {
	if s.driver == nil {
		return textResult("no game running: call drive_start first (with gl: true, to see it)", true)
	}
	s.frames++
	path := filepath.Join(s.work, fmt.Sprintf("frame-%d.png", s.frames))
	if reply, failed := s.driveCommand("capture " + path); failed {
		if strings.Contains(reply, "JM_RENDERER=none") {
			return textResult("this game was started without GL: drive_start with gl: true to see it (state has the draw list without)", true)
		}
		return textResult(reply, true)
	}
	data := jpegOf(path, 960)
	if data == nil {
		return textResult("couldn't read the frame", true)
	}
	var state struct {
		State struct {
			Frame uint64 `json:"frame"`
		} `json:"state"`
	}
	reply, _ := s.driveCommand("state session")
	_ = json.Unmarshal([]byte(reply), &state)
	return toolResult{
		Text:       fmt.Sprintf("Frame %d of the driven game.", state.State.Frame),
		Images:     []mcpImage{{data, "image/jpeg"}},
		Structured: map[string]any{"frame": state.State.Frame},
		Meta:       map[string]any{"jm/image": "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(data)},
	}
}

// projectResources lists the files an agent reads and writes, and the schema.
func projectResources() []map[string]any {
	resources := []map[string]any{{"uri": "jm://schema", "name": "schema", "mimeType": "application/json",
		"description": "every component's scene keys and script fields (jm schema)"}}
	for _, t := range docs.Topics() {
		resources = append(resources, map[string]any{"uri": "jm://docs/" + t.Name, "name": "docs/" + t.Name,
			"mimeType": "text/markdown", "description": t.Title})
	}
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
	if topic, ok := strings.CutPrefix(uri, "jm://docs/"); ok {
		text, err := docs.Read(topic)
		return text, "text/markdown", err
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
