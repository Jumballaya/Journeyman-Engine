package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
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
games folder: $JM_GAMES, else ~/Journeyman. games lists those and every game
the person opened (jm projects); project_open_editor, project_play and
project_export act on any of them without opening it here.

For hosts without a shell (ChatGPT), the open game's files are tools too:
files (jm files: the tree, git's marks), file_read and file_write (an etag
keeps a save from going over someone's change; jm's JSON is saved in jm fmt's
layout), file_move, file_delete, files_search (jm files search) and file_open
(the file in the person's code editor). asset_add saves a file the person
attached in ChatGPT (an image, sprite sheet, Tiled map, Aseprite file, sound
or font, up to 32 MB, over https) into assets/. The game's design notes (design/,
jm memory) are memory, memory_search, memory_note and memory_decide, and the
prompt new_game_interview plans a game with the person into them. Its
history (jm vcs) is vcs_status (a scene's changes per entity), vcs_diff,
vcs_commit, vcs_log and vcs_restore; its variants (jm variant: branches in
their own folders) are variants, variant_new, variant_play, variant_compare,
variant_merge and variant_drop; throwaway copies for experiments (jm sandbox)
are sandboxes, sandbox_new, sandbox_run, sandbox_promote and sandbox_drop.
Its task board (jm tasks) is tasks, task_add, task_move and task_remove.

editor_state and editor_show bridge to the editor on the person's screen:
what they have open and selected and the notes they point out to their agent
there (Edit > Point Out to Your Agent), and showing them a scene or an
entity there (opened, selected, framed, with a note). scene_render draws a
scene in the engine, headless. play_live steers the game the person is
playing (pause, step, slow, watch, set, mark, profile, a picture).
game_index links the game's files (uses, used by, unused); health runs
doctor, build, tests and goldens in one answer. file_view shows a prefab,
an atlas or key bindings (ChatGPT desktop opens those files in it, and
bindings are changed there); game_log is the game's last log.

Hosts that show apps (ChatGPT, Claude) get pages too: games and studio are
Journeyman Studio (ChatGPT's sidebar and a chat's tabs: the games to open,
play, export or make, then the open game's tabs beside the editor; studio's
route shows a page, e.g. "/game"), play_show the Play review timeline, and
scene_view a scene laid out with an inspector (ChatGPT desktop opens
.scene.json files in it and saves changes to the file).

settings_read and settings_update are the open game's settings (name, start
scene, window, logical size in .jm.json), the editor's preferences and the
person's preferences for how agents work (agent.*) as typed fields by key:
ChatGPT shows them on Journeyman's settings page.

Hosts that show forms ask the person instead of guessing: new_game with no
name (what to start from, the name, the picture's shape), project_export with
no target (platform, server, Mac signing) and pick (the game's scenes,
prefabs, textures... in a picker with previews). Clients of the 2026-07-28
protocol get them as input_required results (any transport); older ones as
requests to the client, over stdio.

Over stdio, long calls send notifications/progress (given a progressToken),
notifications/cancelled kills what a call runs (a drive's game too), and the
engine's log and jm's warnings come as notifications/message (logging/setLevel; default warning).

resources/templates/list gives the jm:// shapes resources/read answers (a
game's file, play, marker or entity as jm://game/{game}/…, and docs). Every
URI jm hands out names its game, so it reads the same after open_game; a bare
jm://play/… (as a game's tasks write it) is the open game's.
completion/complete fills in their parts (games, files, scenes, plays,
markers, entity names). Over stdio, resources/subscribe sends
notifications/resources/updated when a subscribed file, scene or play
changes, and notifications/resources/list_changed when the game's files or
plays do. A server started in no game opens the client's root that's a game
(roots/list), before the game opened last.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		settlePathSettings()
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

// mcpInstructions lead with the tandem loop: hosts like Codex copy them into
// every tool's description, so they stay short and say when to call what.
const mcpInstructions = "Journeyman: you and the person build a 2D game together. They play it; you change it. " +
	"Every time they play, the play is recorded, and F8 marks moments they want you to see. " +
	"Whenever the person mentions playing or how the game feels (floaty, slow, unfair, a death, a bug, \"my marker\", \"just now\"), " +
	"or asks how the game is going or what to work on, call play_show first: it opens their latest play as a timeline they can scrub, " +
	"and gives you what happened and their markers. Then play_frame / play_state at a marker (at: \"m1\") to see what they saw, play_inputs for what they pressed. Look before you answer or edit. " +
	"A moment (a marker with picks) says what they meant: play_moment shows the picks outlined; each entity pick's origin is the file to change " +
	"(a scene's entry, or the prefab and the script that spawned it); a point or area pick gives world coordinates, the entities and tile map cells there; a pick's note is what the person said about it; play_state at its frame for more. " +
	"After a change: build, play_verify on their play, then offer play_resume so they try it from the same spot. " +
	"Use these tools instead of running jm in a shell: the same commands, but play_show shows the person the timeline. " +
	"To play it yourself: drive_start (gl: true to see, record: true to keep it), drive, drive_frame, drive_stop, then play_show. " +
	"The game is files (scenes and prefabs JSON, AssemblyScript scripts, HTML/CSS UI): edit them directly; schema lists component keys. " +
	"No game open? games, then open_game or new_game."

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
	Data    any    `json:"data,omitempty"`
}

func newMCPServer() *mcpServer {
	server := &mcpServer{inflight: inflight{calls: map[string]runningCall{}}, watch: resourceWatch{stamps: map[string]string{}}, pollEvery: time.Second, home: gamesHome()}
	server.logLevel.Store(defaultLogLevel)
	for _, tools := range [][]mcpTool{server.gameTools(), server.makeTools(), server.driveTools(), server.playTools(), server.mentionTools(), server.sceneTools(), server.studioTools(), server.fileTools(), server.assetTools(), server.memoryTools(), server.worldTools(), server.playbookTools(), server.vcsTools(), server.variantTools(), server.sandboxTools(), server.taskTools(), server.editorTools(), settingsTools(), {pickTool(server), companionsTool(), jsonEditTool()}, server.liveTools(), server.intelTools(), server.fileViewTools(), server.consoleTools()} {
		server.tools = append(server.tools, tools...)
	}
	for i, tool := range server.tools {
		if _, entry := tool.Meta["openai/ui"]; entry { // opened from the host's sidebar or tabs: jm's mark there
			server.tools[i].Icons = serverIcons()
		}
		if given := btoi(tool.run != nil) + btoi(tool.onGame != nil) + btoi(tool.onDriven != nil); given != 1 {
			panic("tool " + tool.Name + ": give one of run, onGame and onDriven")
		}
		if tool.run == nil {
			server.tools[i].run = func(a toolArgs) toolResult { _, run := server.prepare(tool, a); return run() }
			props, _ := tool.InputSchema["properties"].(map[string]any)
			if props == nil {
				props = map[string]any{}
				tool.InputSchema["properties"] = props
			}
			props["game"] = map[bool]map[string]any{true: gameOfTool, false: drivenArg}[tool.onGame != nil]
		}
	}
	if cwd, err := os.Getwd(); err == nil && exists(archive.ManifestEntryKey) { // started in a game (or --dir)
		server.open.Store(evalOrSelf(cwd))
	}
	return server
}

// pathSettings name machine things by path (the engine, the editor, temp
// folders…); a relative one means from where jm mcp started.
var pathSettings = []string{"JM_ENGINE", "JM_SERVER", "JM_PLAYER", "JM_PLAYERS", "JM_EDITOR", "JM_GAMES", "JM_TOOLCHAIN_DIR", "TMPDIR", "TMP", "TEMP"}

// settlePathSettings makes each absolute, once at startup: the server's tools
// and the jm commands they run work in game folders, never where it started.
func settlePathSettings() {
	for _, key := range pathSettings {
		if v := os.Getenv(key); v != "" && !filepath.IsAbs(v) {
			if abs, err := filepath.Abs(v); err == nil {
				os.Setenv(key, abs)
			}
		}
	}
}

func serveMCP(in io.Reader, out io.Writer) error { return newMCPServer().serve(in, out) }

func (server *mcpServer) serve(in io.Reader, out io.Writer) error {
	defer server.stopDriver()
	stopOnSignal(server)
	server.peer = newMCPPeer(out)
	// Messages are answered in order on their own goroutine, so a reply to
	// the server's own request (a form) still gets read while a tool waits.
	requests, done := make(chan []byte, 1024), make(chan struct{})
	server.requests = requests
	go func() {
		defer close(done)
		for data := range requests {
			server.answer(data)
		}
	}()
	stopWatching := every(server.pollEvery, server.pollResources)
	scanner := bufio.NewScanner(in)
	scanner.Buffer(make([]byte, 1<<20), 64<<20)
	for scanner.Scan() {
		if data := bytes.Clone(bytes.TrimSpace(scanner.Bytes())); !server.peer.deliver(data) && !server.control(data) {
			requests <- data
		}
	}
	stopWatching()
	server.peer.close()
	close(requests)
	<-done
	server.rootsWork.Wait() // their replies closed with the input: they're ending
	return scanner.Err()
}

// stopOnSignal ends the driven game (and removes its folder) when the client
// stops the server with a signal rather than closing its input.
func stopOnSignal(s *mcpServer) {
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-signals
		s.inflight.cancel(nil) // its children are in their own process groups: the signal didn't reach them
		s.calls.Lock()         // after any call running now
		s.stopDriver()
		os.Exit(1)
	}()
}

type mcpServer struct {
	tools []mcpTool
	calls sync.Mutex  // one tool call at a time: the driver and the project are shared
	game  *drivenGame // the driven game, while one runs
	peer  *mcpPeer    // the client, where the transport lets the server ask it (stdio)
	forms formSupport // what forms the client shows, from its initialize
	round *formRound  // the modern tool call running, if one is: its forms (mcp_mrtr.go)
	// stdio's messages waiting to be answered: a tool waiting on the client answers them meanwhile.
	requests  <-chan []byte
	inflight  inflight       // the tool calls not answered yet, for a cancel to find
	logLevel  atomic.Int32   // the least severe log message the client wants (logLevels)
	call      *toolCall      // the tool call running, if one is
	watch     resourceWatch  // what the client subscribed to (mcp_resources.go)
	pollEvery time.Duration  // how often subscriptions are checked
	roots     rootsState     // the client's roots, for the game to open
	home      string         // the games folder, absolute
	rootsWork sync.WaitGroup // roots answers being acted on
	byRecent  bool           // the open game is only the one opened last: the client's root beats it
	open      atomic.Value   // string: the game calls work on when they name none; only setOpen changes it (resource polls read it unlocked)
}

func (s *mcpServer) answer(data []byte) {
	if reply, ok := s.respond(data); ok {
		s.peer.send(reply)
	}
}

// await is the client's reply to the server's request, answering the
// client's own messages meanwhile (a picker's previews) with the calls lock
// let go: the caller holds it, and gets it back. Forms nest: one opened
// meanwhile is answered first.
func (s *mcpServer) await(reply <-chan rpcMessage) (rpcMessage, bool) {
	cancelled := s.call.context().Done() // a cancelled call stops waiting
	s.calls.Unlock()
	defer s.calls.Lock()
	requests := s.requests
	for {
		select {
		case msg, ok := <-reply:
			return msg, ok
		case <-cancelled:
			s.peer.abandon(reply)
			return rpcMessage{}, false
		case data, open := <-requests:
			if !open {
				requests = nil // input ended: the reply channel closes too
				continue
			}
			s.answer(data)
		}
	}
}

// respond answers one JSON-RPC message, whichever transport brought it; false
// for a notification (nothing to answer).
func (s *mcpServer) respond(data []byte) (rpcMessage, bool) {
	var msg rpcMessage
	if len(data) == 0 {
		return msg, false
	}
	if err := json.Unmarshal(data, &msg); err != nil {
		return rpcMessage{JSONRPC: "2.0", ID: json.RawMessage("null"), Error: &rpcError{Code: -32700, Message: "parse error: " + err.Error()}}, true
	}
	if msg.ID == nil || msg.Method == "" {
		switch msg.Method {
		case "notifications/cancelled": // over HTTP: stdio's took it as it came (control)
			s.control(data)
		case "notifications/initialized", "notifications/roots/list_changed":
			s.askRoots()
		}
		return msg, false // a notification (initialized, cancelled), or a reply to us
	}
	reply := rpcMessage{JSONRPC: "2.0", ID: msg.ID}
	ctx := context.Background()
	if msg.Method == "tools/call" {
		ctx = s.inflight.begin(msg.ID)
		defer s.inflight.end(msg.ID) // a malformed one too
	}
	meta, err := metaOf(msg.Params)
	if err != nil {
		reply.Error = &rpcError{Code: -32602, Message: err.Error()}
		return reply, true
	}
	switch meta.Version {
	case "", mcpProtocolVersion: // legacy: the session's version, from initialize
		reply.Result, reply.Error = s.handle(ctx, msg.Method, msg.Params)
	case mcpModernVersion:
		reply.Result, reply.Error = s.handleModern(ctx, msg.Method, msg.Params, meta)
	default:
		reply.Error = &rpcError{Code: -32022, Message: "Unsupported protocol version",
			Data: map[string]any{"supported": []string{mcpModernVersion, mcpProtocolVersion}, "requested": meta.Version}}
	}
	if context.Cause(ctx) == errCancelledByClient {
		return msg, false // the client wants no answer
	}
	return reply, true
}

// handleModern answers a request of the modern protocol: its result says
// its type, and a tool call can ask the person (formRound).
func (s *mcpServer) handleModern(ctx context.Context, method string, params json.RawMessage, meta requestMeta) (any, *rpcError) {
	if method == "tools/call" {
		round, err := newRound(formsOf(meta.Caps, true), params)
		if err != nil {
			return nil, &rpcError{Code: -32602, Message: err.Error()}
		}
		result, rpcErr := s.callTool(ctx, params, round)
		if round.pending != nil {
			return round.pending, nil
		}
		return complete(result), rpcErr
	}
	result, rpcErr := s.handle(ctx, method, params)
	if rpcErr != nil && rpcErr.Code == -32002 { // not found: earlier versions' code
		rpcErr.Code = -32602
	}
	if m, ok := result.(map[string]any); ok && (method == "tools/list" || method == "resources/list" || method == "resources/read") {
		m["ttlMs"], m["cacheScope"] = 0, "private" // the open game's, and it changes: don't keep it
	}
	return complete(result), rpcErr
}

// complete marks a result as the request's whole answer.
func complete(result any) any {
	if m, ok := result.(map[string]any); ok {
		if _, typed := m["resultType"]; !typed {
			m["resultType"] = "complete"
		}
	}
	return result
}

func (s *mcpServer) handle(ctx context.Context, method string, params json.RawMessage) (any, *rpcError) {
	switch method {
	case "initialize":
		s.calls.Lock()
		var init struct {
			Capabilities clientCaps `json:"capabilities"`
		}
		_ = json.Unmarshal(params, &init)
		s.forms = formsOf(init.Capabilities, false)
		s.calls.Unlock()
		s.roots.mu.Lock()
		s.roots.listed = init.Capabilities.Roots != nil
		s.roots.mu.Unlock()
		return map[string]any{
			"protocolVersion": mcpProtocolVersion,
			"capabilities":    s.capabilities(),
			"serverInfo":      mcpServerInfo(),
			"instructions":    mcpInstructions,
		}, nil
	case "server/discover":
		// Newer clients ask this first: the modern version (mcp_mrtr.go), else they initialize.
		return map[string]any{
			"resultType":        "complete",
			"supportedVersions": []string{mcpModernVersion, mcpProtocolVersion},
			"capabilities":      s.capabilities(),
			"_meta":             map[string]any{"io.modelcontextprotocol/serverInfo": mcpServerInfo()},
			"instructions":      mcpInstructions,
			"ttlMs":             0,
			"cacheScope":        "private",
		}, nil
	case "ping":
		return map[string]any{}, nil
	case "tools/list":
		return map[string]any{"tools": s.tools}, nil
	case "tools/call":
		return s.callTool(ctx, params, nil)
	case "logging/setLevel":
		return s.setLogLevel(params)
	case "prompts/list":
		return map[string]any{"prompts": promptList()}, nil
	case "prompts/get":
		var get struct {
			Name      string            `json:"name"`
			Arguments map[string]string `json:"arguments"`
		}
		_ = json.Unmarshal(params, &get)
		s.calls.Lock() // its links are the open game's notes'
		s.openRecentIfNone()
		root := s.opened()
		s.calls.Unlock()
		prompt, err := getPrompt(root, get.Name, get.Arguments)
		if err != nil {
			return nil, err // a nil map in an any isn't nil
		}
		return prompt, nil
	case "resources/list":
		s.calls.Lock() // resources are the open game's: not while open_game switches it
		defer s.calls.Unlock()
		s.openRecentIfNone()
		if s.peer != nil {
			s.watch.listedNow(s.opened())
		}
		return map[string]any{"resources": append(widgetResources(), projectResources(s.opened())...)}, nil
	case "resources/templates/list":
		return map[string]any{"resourceTemplates": resourceTemplates()}, nil
	case "resources/subscribe", "resources/unsubscribe":
		return s.subscribe(params, method == "resources/subscribe")
	case "completion/complete":
		return s.completeArgument(params)
	case "resources/read":
		s.calls.Lock()
		defer s.calls.Unlock()
		s.openRecentIfNone()
		var read struct {
			URI string `json:"uri"`
		}
		if err := json.Unmarshal(params, &read); err != nil {
			return nil, &rpcError{Code: -32602, Message: "invalid params: " + err.Error()}
		}
		contents, err := s.readURI(read.URI)
		if err != nil {
			return nil, &rpcError{Code: -32002, Message: err.Error()}
		}
		return map[string]any{"contents": contents}, nil
	}
	return nil, &rpcError{Code: -32601, Message: "method not found: " + method}
}

// callTool runs the tool params name until ctx is cancelled; round: a modern call's forms (nil: legacy).
func (s *mcpServer) callTool(ctx context.Context, params json.RawMessage, round *formRound) (any, *rpcError) {
	var call struct {
		Name      string         `json:"name"`
		Arguments map[string]any `json:"arguments"`
		Meta      struct {
			Resource struct {
				Path string `json:"path"`
			} `json:"openai/resource"`
		} `json:"_meta"`
	}
	if err := json.Unmarshal(params, &call); err != nil {
		return nil, &rpcError{Code: -32602, Message: "invalid params: " + err.Error()}
	}
	for _, tool := range s.tools {
		if tool.Name == call.Name {
			if call.Arguments == nil {
				call.Arguments = map[string]any{}
			}
			if path := call.Meta.Resource.Path; path != "" {
				call.Arguments[resourceKey] = resourcePath(path)
			}
			s.calls.Lock()
			defer s.calls.Unlock()
			s.openRecentIfNone()
			game, run := s.prepare(tool, call.Arguments)
			if round != nil { // the game it runs in, now it's settled
				if err := round.bind(game); err != nil {
					return nil, &rpcError{Code: -32602, Message: err.Error()}
				}
			}
			if ctx.Err() != nil { // cancelled while it waited its turn
				return nil, &rpcError{Code: -32800, Message: "cancelled"}
			}
			c := s.newCall(ctx, params)
			call.Arguments[callKey] = c
			outer := s.call // a form's wait can run a call inside this one
			s.round, s.call = round, c
			defer func() { s.round, s.call = nil, outer }() // before the unlock
			defer c.followEngineLog(game)()
			return run().reply(), nil
		}
	}
	return nil, &rpcError{Code: -32602, Message: "unknown tool " + call.Name}
}

// mcpCapabilities: extensions (MCP Apps, OpenAI's) both where current
// protocols look and where older ones did (experimental).
func mcpCapabilities() map[string]any {
	extensions := map[string]any{"io.modelcontextprotocol/ui": map[string]any{}, "openai/mentions": map[string]any{"searchTool": mentionTool},
		"openai/settings": map[string]any{"readTool": settingsReadTool, "updateTool": settingsUpdateTool}}
	return map[string]any{"tools": map[string]any{}, "resources": map[string]any{}, "prompts": map[string]any{}, "logging": map[string]any{},
		"completions": map[string]any{}, "extensions": extensions, "experimental": extensions}
}

// capabilities are mcpCapabilities with what this transport can do: stdio
// can tell the client a resource changed; HTTP answers only what's asked.
func (s *mcpServer) capabilities() map[string]any {
	caps := mcpCapabilities()
	if s.peer != nil {
		caps["resources"] = map[string]any{"subscribe": true, "listChanged": true}
	}
	return caps
}

func mcpServerInfo() map[string]any {
	return map[string]any{"name": "journeyman", "title": "Journeyman", "version": version, "icons": serverIcons(),
		"websiteUrl": "https://github.com/Jumballaya/Journeyman-Engine"}
}
