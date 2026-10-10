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
	"os/signal"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"syscall"

	"github.com/Jumballaya/Journeyman-Engine/internal/docs"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"

	"github.com/spf13/cobra"
)

// jm mcp: the CLI as an MCP server, for agents that speak MCP. A thin layer
// by design: tools run jm's own commands and functions (and the engine's
// stepped driver), so there's nothing MCP can do that jm can't. Resources are
// the project's files, the docs and the engine's schema.

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

Register it with an MCP client as the command "jm mcp" (jm setup does it),
run in the project. Started anywhere else (Claude Desktop starts it in no
folder), the games / new_game / open_game tools make and open games in the
games folder: $JM_GAMES, else ~/Journeyman.`,
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
	run  func(args toolArgs) toolResult
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

func newMCPServer() *mcpServer {
	server := &mcpServer{}
	server.tools = append(append(server.gameTools(), server.makeTools()...), server.playTools()...)
	return server
}

func serveMCP(in io.Reader, out io.Writer) error {
	server := newMCPServer()
	defer server.stopDriver()
	stopOnSignal(server)
	scanner := bufio.NewScanner(in)
	scanner.Buffer(make([]byte, 1<<20), 64<<20)
	for scanner.Scan() {
		if reply, ok := server.respond(bytes.TrimSpace(scanner.Bytes())); ok {
			data, _ := json.Marshal(reply)
			fmt.Fprintf(out, "%s\n", data)
		}
	}
	return scanner.Err()
}

// stopOnSignal ends the driven game (and removes its folder) when the client
// stops the server with a signal rather than closing its input.
func stopOnSignal(s *mcpServer) {
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-signals
		s.calls.Lock() // after any call running now
		s.stopDriver()
		os.Exit(1)
	}()
}

type mcpServer struct {
	tools []mcpTool
	calls sync.Mutex  // one tool call at a time: the driver and the project are shared
	game  *drivenGame // the driven game, while one runs
}

// respond answers one JSON-RPC message, whichever transport brought it; false
// for a notification (nothing to answer).
func (s *mcpServer) respond(data []byte) (rpcMessage, bool) {
	var msg rpcMessage
	if len(data) == 0 {
		return msg, false
	}
	if err := json.Unmarshal(data, &msg); err != nil {
		return rpcMessage{JSONRPC: "2.0", ID: json.RawMessage("null"), Error: &rpcError{-32700, "parse error: " + err.Error()}}, true
	}
	if msg.ID == nil || msg.Method == "" {
		return msg, false // a notification (initialized, cancelled), or a reply to us
	}
	reply := rpcMessage{JSONRPC: "2.0", ID: msg.ID}
	reply.Result, reply.Error = s.handle(msg.Method, msg.Params)
	return reply, true
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
				"If no game is open (call games), ask the person what they want to make and call new_game, or open_game for one they have. " +
				"Read jm://docs/agents first (the workflow), jm://schema for every component's keys, and jm://docs/scripting for the script API. " +
				"To play the game yourself and see it, use the driver, not the game's or the editor's window: drive_start (gl: true to see it, " +
				"record: true to keep the run as a play, visible: true so the person can watch), drive (keys, clicks, steps: exact and repeatable), " +
				"drive_frame (the screen now, as an image), drive_stop (gives the play's id; play_show then shows it to the person). " +
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
				s.calls.Lock()
				defer s.calls.Unlock()
				return tool.run(call.Arguments).reply(), nil
			}
		}
		return nil, &rpcError{-32602, "unknown tool " + call.Name}
	case "resources/list":
		s.calls.Lock() // resources are the open game's: not while open_game switches it
		defer s.calls.Unlock()
		return map[string]any{"resources": append(timelineResources(), projectResources()...)}, nil
	case "resources/read":
		s.calls.Lock()
		defer s.calls.Unlock()
		var read struct {
			URI string `json:"uri"`
		}
		if err := json.Unmarshal(params, &read); err != nil {
			return nil, &rpcError{-32602, "invalid params: " + err.Error()}
		}
		if content, ok := readTimeline(read.URI); ok {
			return map[string]any{"contents": []map[string]any{content}}, nil
		}
		text, mime, err := readResource(read.URI)
		if err != nil {
			return nil, &rpcError{-32002, err.Error()}
		}
		return map[string]any{"contents": []map[string]any{{"uri": read.URI, "mimeType": mime, "text": text}}}, nil
	}
	return nil, &rpcError{-32601, "method not found: " + method}
}

// toolArgs are a tool call's arguments, read leniently: a missing or
// mistyped one is its zero value, and the tool's default applies.
type toolArgs map[string]any

func (a toolArgs) str(key string) string {
	v, _ := a[key].(string)
	return v
}

func (a toolArgs) flag(key string) bool {
	v, _ := a[key].(bool)
	return v
}

// num is a numeric argument: fallback when absent, else clamped to [lo, hi].
func (a toolArgs) num(key string, fallback, lo, hi float64) float64 {
	v, ok := a[key].(float64)
	if !ok {
		return fallback
	}
	return min(max(v, lo), hi)
}

func (a toolArgs) list(key string) []string {
	items, _ := a[key].([]any)
	out := []string{}
	for _, item := range items {
		out = append(out, fmt.Sprint(item))
	}
	return out
}

// Schemas for tool arguments.
func object(properties map[string]any, required ...string) map[string]any {
	schema := map[string]any{"type": "object", "properties": properties}
	if len(required) > 0 {
		schema["required"] = required
	}
	return schema
}

func strArg(description string) map[string]any {
	return map[string]any{"type": "string", "description": description}
}

func boolArg(description string) map[string]any {
	return map[string]any{"type": "boolean", "description": description}
}

func intArg(description string) map[string]any {
	return map[string]any{"type": "integer", "description": description}
}

func listArg(description string) map[string]any {
	return map[string]any{"type": "array", "items": map[string]any{"type": "string"}, "description": description}
}

// readOnly marks a tool that only looks: hosts run it without asking first.
func readOnly() map[string]any { return map[string]any{"readOnlyHint": true, "openWorldHint": false} }

// writes marks a tool that changes the project or runs the game (not destructively).
func writes() map[string]any {
	return map[string]any{"readOnlyHint": false, "destructiveHint": false, "openWorldHint": false}
}

// jmTool runs a jm command whose arguments args builds.
func jmTool(args func(a toolArgs) ([]string, error)) func(toolArgs) toolResult {
	return func(a toolArgs) toolResult {
		list, err := args(a)
		if err != nil {
			return textResult(err.Error(), true)
		}
		return textResult(runJM(list...))
	}
}

// positional ends a command's flags: what follows is never read as one
// ("--engine=/bin/sh" stays a name).
func positional(command []string, args ...string) []string {
	if len(args) == 0 {
		return command
	}
	return append(append(command, "--"), args...)
}

// projectFiles are paths that must be the project's own files, as paths
// from the project's folder.
func projectFiles(paths []string) ([]string, error) {
	out := []string{}
	for _, p := range paths {
		rel, err := projectFile(p)
		if err != nil {
			return nil, err
		}
		out = append(out, rel)
	}
	return out, nil
}

// projectFile is path (from the project's folder, or absolute) as a path from
// the project's folder, symlinks resolved: one that leads out isn't the project's.
func projectFile(path string) (string, error) {
	root, err := realPath(".")
	if err != nil {
		return "", err
	}
	real, err := realPath(path)
	if err != nil {
		return "", fmt.Errorf("%s: no such file in the project", path)
	}
	rel, err := filepath.Rel(root, real)
	if err != nil || rel == ".." || strings.HasPrefix(rel, ".."+string(filepath.Separator)) {
		return "", fmt.Errorf("%s is outside the project", path)
	}
	return rel, nil
}

// realPath is path absolute, with every symlink on the way resolved.
func realPath(path string) (string, error) {
	abs, err := filepath.Abs(path)
	if err != nil {
		return "", err
	}
	return filepath.EvalSymlinks(abs)
}

// with is args, plus flag when on.
func with(args []string, on bool, flag ...string) []string {
	if on {
		return append(args, flag...)
	}
	return args
}

func (s *mcpServer) makeTools() []mcpTool {
	return []mcpTool{
		{Name: "build", Description: "jm build --json: compile scripts, bake atlases, check scenes and prefabs. JSON lines; the last is the result.",
			Annotations: writes(), InputSchema: object(map[string]any{}),
			run: jmTool(func(toolArgs) ([]string, error) { return []string{"build", "--json"}, nil })},
		{Name: "doctor", Description: "jm doctor --json: jm's and the engine's versions, the script toolchain (Node, AssemblyScript), the project, and any problems with their fixes.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}),
			run: jmTool(func(toolArgs) ([]string, error) { return []string{"doctor", "--json"}, nil })},
		{Name: "test", Description: "jm test --json: run tests/*.spec.ts (game logic, no engine). A JSON line per test; the last is the result.",
			Annotations: writes(), InputSchema: object(map[string]any{"specs": listArg("spec files (default: all)")}),
			run: jmTool(func(a toolArgs) ([]string, error) {
				specs, err := projectFiles(a.list("specs"))
				return positional([]string{"test", "--json"}, specs...), err
			})},
		{Name: "golden", Description: "jm golden --json: compare frames with tests/golden images (update: record them instead). Build first.",
			Annotations: writes(), InputSchema: object(map[string]any{"names": listArg("goldens to check (default: all)"), "update": boolArg("record the frames as the new goldens")}),
			run: jmTool(func(a toolArgs) ([]string, error) {
				return positional(with([]string{"golden", "--json"}, a.flag("update"), "--update"), a.list("names")...), nil
			})},
		{Name: "schema", Description: "jm schema: every component's scene keys and script fields (or one component's), as JSON.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"component": strArg("e.g. SpriteComponent (default: all)")}),
			run: jmTool(func(a toolArgs) ([]string, error) {
				if c := a.str("component"); c != "" {
					return positional([]string{"schema"}, c), nil
				}
				return []string{"schema"}, nil
			})},
		{Name: "generate", Description: "jm generate <kind> <name>: make a file from a template (jm generate list shows the kinds).",
			Annotations: writes(), InputSchema: object(map[string]any{"kind": strArg("e.g. prefab, script, scene, ui, shader, bindings, list"), "name": strArg("the new file's name")}, "kind"),
			run: jmTool(func(a toolArgs) ([]string, error) {
				if a.str("name") == "" {
					return positional([]string{"generate"}, a.str("kind")), nil
				}
				return positional([]string{"generate"}, a.str("kind"), a.str("name")), nil
			})},
		{Name: "fmt", Description: "jm fmt: write the project's JSON (scenes, prefabs, data, the manifest) in the layout the editor writes, so diffs stay small. Run it before committing.",
			Annotations: writes(), InputSchema: object(map[string]any{
				"files": listArg("only these files (default: the whole project)"),
				"check": boolArg("change nothing; fail if a file needs formatting"),
			}),
			run: jmTool(func(a toolArgs) ([]string, error) {
				files, err := projectFiles(a.list("files"))
				return positional(with([]string{"fmt"}, a.flag("check"), "--check"), files...), err
			})},
		{Name: "export", Description: "jm export: build the game and write a standalone executable with everything inside (an .app on macOS) to dist/, for the person to run or share.",
			Annotations: writes(), InputSchema: object(map[string]any{
				"target": strArg("platform as os-arch, e.g. windows-amd64 (default: this machine; others need that platform's engine as player)"),
				"server": boolArg("export the dedicated multiplayer server instead"),
			}),
			run: jmTool(func(a toolArgs) ([]string, error) {
				return with(with([]string{"export"}, a.str("target") != "", "--target", a.str("target")), a.flag("server"), "--server"), nil
			})},
		{Name: "drive_start", Description: "Start the built game under the stepped driver (headless; no window or GL unless gl is true). " +
			"It builds the game first when the build is missing or older than the sources. " +
			"It waits at frame 0 until told to step, or with play, at that moment of the person's recorded play. One game at a time; starting again restarts it. " +
			"With record, what you play is recorded as a play like the person's (drive_stop gives its id; play_show, play_frame and the timeline then work on it).",
			Annotations: writes(), InputSchema: object(map[string]any{
				"scene":   strArg("start in this scene instead of the entry scene"),
				"session": map[string]any{"type": "object", "description": "game state set before the first frame (a deep link)"},
				"gl":      boolArg("render with OpenGL, so drive_frame can show the game (needs a display)"),
				"play":    strArg("start at a moment of a recorded play instead (plays_list): replayed exactly up to there, then yours to drive"),
				"at":      strArg(`with play: the moment, e.g. "m2", "12.5s", a frame (default: its end)`),
				"record":  boolArg("record this run as a play (drive_stop gives its id)"),
				"seed":    intArg("the run's random seed (default: a new one; a play replays its own)"),
				"visible": boolArg("show the game in a window as you drive it, so the person can watch (implies gl)"),
			}),
			run: s.startDriver},
		{Name: "drive", Description: "Driver commands, each answered as JSON: step [n] [dt], state [part...] [tag=Name...] [Component...] (e.g. state session tag=Player), get [tag=Name] <path> (get tag=Ball TransformComponent.x), " +
			"down|up|press <Key>, move x y, click [x y], wheel dy, set <key> <json>, scene <path>, marker [note] (marks the moment in a recorded run, as F8 does), " +
			"until [tag=Name] <path> <op> <value> [max n] (steps until it's true, e.g. until tag=Lift TransformComponent.y < -270), echo <text>, " +
			"near tag=Name [distance] (near misses: what's within distance, with the gaps), " +
			"debug physics on|off (colliders and terrain drawn over the frame), quit (drive_frame shows the game). " +
			"To follow something frame by frame, send commands with repeat instead of a call per frame: " +
			"commands [\"step 1\", \"get tag=Player TransformComponent.y\"], repeat 40 (a reply line each). " +
			"A failing command stops the batch: the commands before it already ran, and the result is an error holding the replies so far. " +
			"Past 1 MB of replies the batch stops too (the last line says {\"truncated\":true,\"after\":n commands}): ask for less, e.g. get instead of state.",
			Annotations: writes(), InputSchema: object(map[string]any{
				"command":  strArg(`e.g. "step 60", "press Enter", "state"`),
				"commands": listArg("several commands in order, instead of command"),
				"repeat":   intArg("run commands this many times over (default 1, at most 1000)"),
			}),
			run: func(a toolArgs) toolResult {
				commands := a.list("commands")
				if len(commands) == 0 && a.str("command") != "" {
					commands = []string{a.str("command")}
				}
				if len(commands) == 0 {
					return textResult(`give a command, e.g. "step 60", or commands`, true)
				}
				return textResult(s.driveBatch(commands, int(a.num("repeat", 1, 1, 1000)), driveReplyLimit))
			}},
		{Name: "drive_frame", Title: "See the driven game", Description: "An image of the driven game as it is now (the last frame it ran). " +
			"Needs a game started with gl: true. Use it to see what your commands did, e.g. after step 60 or a click.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}), run: s.driveFrame},
		{Name: "drive_stop", Description: "Stop the driven game; a recorded run's play id comes back.", Annotations: writes(), InputSchema: object(map[string]any{}),
			run: func(toolArgs) toolResult {
				play := s.stopDriver()
				if play == "" {
					return textResult(`{"ok":true}`, false)
				}
				return jsonResult(map[string]any{"ok": true, "play": play, "next": "play_show shows it, as it does the person's plays"})
			}},
		{Name: "session", Description: "Play a multiplayer session on this machine (jm run --peers), headless with no GL, " +
			"in real time: the game's server if it has one, and N games. Each game can replay its own input. " +
			"Returns each peer's state when it ended: its net section (role, player, players, shared entities), " +
			"scene, session store and shared entities' tags and fields. Build first.",
			Annotations: writes(), InputSchema: object(map[string]any{
				"peers":   intArg("how many games (default 2)"),
				"frames":  intArg("frames each game runs, 60 a second (default 300)"),
				"replays": listArg(`replay text per game, in order ("30 down ArrowRight" lines)`),
				"latency": intArg("simulated latency per message, ms"),
				"loss":    map[string]any{"type": "number", "description": "simulated loss of unreliable messages, 0..1"},
			}),
			run: runSessionTool},
	}
}

// jsonResult is a value as compact JSON for the model and as structured
// content for hosts (which takes an object).
func jsonResult(v any) toolResult {
	data, err := json.Marshal(v)
	if err != nil {
		return textResult(err.Error(), true)
	}
	var structured any
	_ = json.Unmarshal(data, &structured)
	if _, isObject := structured.(map[string]any); !isObject {
		structured = map[string]any{"items": structured}
	}
	return toolResult{Text: string(data), Structured: structured}
}

// runSessionTool is the "session" tool: jm run --peers with every peer's
// final state dumped, then those dumps, trimmed to what a session is about.
func runSessionTool(a toolArgs) toolResult {
	peers, frames := int(a.num("peers", 2, 1, 64)), int(a.num("frames", 300, 1, 1e7))
	work, err := os.MkdirTemp("", "jm-session-")
	if err != nil {
		return textResult(err.Error(), true)
	}
	defer os.RemoveAll(work)
	replays := a.list("replays")
	for i := 1; i <= peers && len(replays) > 0; i++ {
		text := "" // a peer past the list just plays nothing
		if i <= len(replays) {
			text = replays[i-1] + "\n"
		}
		if err := os.WriteFile(filepath.Join(work, fmt.Sprintf("peer%d.txt", i)), []byte(text), 0o644); err != nil {
			return textResult(err.Error(), true)
		}
	}
	args := []string{"run", "--peers", fmt.Sprint(peers)}
	if v := a.num("latency", 0, 0, 1e6); v > 0 {
		args = append(args, "--latency", fmt.Sprint(int(v)))
	}
	if v := a.num("loss", 0, 0, 1); v > 0 {
		args = append(args, "--loss", fmt.Sprint(v))
	}
	self, err := os.Executable()
	if err != nil {
		return textResult(err.Error(), true)
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
	return textResult(string(text), runErr != nil || len(errors) > 0)
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

func (s *mcpServer) startDriver(a toolArgs) toolResult {
	s.stopDriver()
	root, err := projectRoot()
	if err != nil {
		return textResult(err.Error(), true)
	}
	// The build is jm's, not the person's files: one that's missing or older
	// than the sources is made first, so the game driven is the game as it is.
	if buildIsStale(root) {
		if out, failed := runJM("build", "--json"); failed {
			return textResult("the build failed, so there's no game to run:\n"+out, true)
		}
	}
	o := gameOptions{GL: a.flag("gl"), Visible: a.flag("visible"), Scene: a.str("scene"), Record: a.flag("record")}
	o.Session, _ = a["session"].(map[string]any)
	if seed, ok := a["seed"].(float64); ok {
		if seed < 0 || seed != float64(uint64(seed)) {
			return textResult("seed is a whole number, 0 or more", true)
		}
		o.Seed = new(uint64)
		*o.Seed = uint64(seed)
	}
	var from uint64 // with a play: the frame to drive on from
	if ref := a.str("play"); ref != "" {
		if o.Scene != "" || len(o.Session) > 0 || o.Seed != nil {
			return textResult("a play brings its own scene, session and seed: give play (and at), or those", true)
		}
		p, err := plays.Find(root, ref)
		if err == nil {
			from, err = p.FrameAt(a.str("at"))
		}
		if err != nil {
			return textResult(err.Error(), true)
		}
		o.Play, o.Until = p, from+1
	}
	s.game, err = startGame(root, o)
	if err != nil {
		return textResult(err.Error(), true)
	}
	if o.Play != nil {
		// Replay the play up to the moment: the game is then where the person was.
		if err := s.game.to(from); err != nil {
			s.stopDriver()
			return textResult(err.Error(), true)
		}
	}
	return jsonResult(map[string]any{"ok": true, "ready": true, "frame": s.game.frame, "recording": o.Record})
}

// driveCommand sends one command to the driven game: its reply line, and
// whether it failed (the game refused it, or ended).
func (s *mcpServer) driveCommand(command string) (string, bool) {
	if s.game == nil {
		return "no game running: call drive_start first", true
	}
	verb := strings.Fields(command)
	switch {
	case strings.ContainsAny(command, "\n\r"):
		return "one command per call", true
	case len(verb) == 0 || strings.HasPrefix(verb[0], "#"):
		return `give a command, e.g. "step 60" (a blank or # line gets no answer)`, true
	case verb[0] == "capture":
		// The engine writes a capture wherever it's told: not a path to take from a client.
		return "use drive_frame to see the game", true
	}
	line, err := s.game.send(command)
	if err != nil {
		return s.ended(err.Error()), true
	}
	if verb[0] == "quit" {
		return s.ended(line), false
	}
	var reply struct {
		OK bool `json:"ok"`
	}
	_ = json.Unmarshal([]byte(line), &reply)
	return line, !reply.OK
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

// ended stops a game that has ended, adding the play it recorded to reply.
func (s *mcpServer) ended(reply string) string {
	if play := s.stopDriver(); play != "" {
		return reply + "\n" + fmt.Sprintf(`{"play":%q,"next":"play_show shows it"}`, play)
	}
	return reply
}

// stopDriver ends the driven game, if one runs: the id of the play it
// recorded, "" when it didn't.
func (s *mcpServer) stopDriver() string {
	if s.game == nil {
		return ""
	}
	play := s.game.close()
	s.game = nil
	return play
}

// driveFrame is an image of the driven game as it is now (the last frame run):
// the agent gets the image, not a file to manage.
func (s *mcpServer) driveFrame(toolArgs) toolResult {
	if s.game == nil {
		return textResult("no game running: call drive_start first (with gl: true, to see it)", true)
	}
	path, err := s.game.capture()
	if err != nil {
		if strings.Contains(err.Error(), "JM_RENDERER=none") {
			return textResult("this game was started without GL: drive_start with gl: true to see it (state has the draw list without)", true)
		}
		return textResult(err.Error(), true)
	}
	// The last frame run, whose image this is.
	frame := s.game.frame - min(s.game.frame, 1)
	return imageResult(path, fmt.Sprintf("Frame %d of the driven game.", frame), map[string]any{"frame": frame})
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
			if path != "." && hiddenFromAgents(path) {
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

// hiddenFromAgents says whether a folder of the project (a path from its
// folder) is one resources leave out: dot-folders (.git, .jm), build output,
// node_modules, dist.
func hiddenFromAgents(dir string) bool {
	for _, part := range strings.Split(filepath.ToSlash(dir), "/") {
		if part != "." && (strings.HasPrefix(part, ".") || part == "build" || part == "node_modules" || part == "dist") {
			return true
		}
	}
	return false
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
	// The files projectResources lists, and no others.
	rel, err := projectFile(path)
	if err != nil {
		return "", "", err
	}
	if mimeOf(rel) == "" || hiddenFromAgents(filepath.Dir(rel)) {
		return "", "", fmt.Errorf("%s isn't one of the project's resources", uri)
	}
	data, err := os.ReadFile(rel)
	if err != nil {
		return "", "", err
	}
	return string(data), mimeOf(path), nil
}
