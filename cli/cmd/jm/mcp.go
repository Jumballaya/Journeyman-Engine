package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net/url"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/docs"
	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
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

resources/templates/list gives the jm:// shapes resources/read answers (any
game's file as jm://game/{game}/file/{path}, plays, markers, entities, docs),
and completion/complete fills in their parts (games, files, scenes, plays,
markers, entity names). Over stdio, resources/subscribe sends
notifications/resources/updated when a subscribed file, scene or play
changes, and notifications/resources/list_changed when the game's files or
plays do. A server started in no game opens the client's root that's a game
(roots/list), before the game opened last.`,
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

type mcpTool struct {
	Name        string         `json:"name"`
	Title       string         `json:"title,omitempty"`
	Description string         `json:"description"`
	InputSchema map[string]any `json:"inputSchema"`
	// The shape of its structuredContent, when hosts rely on it (openai/settings).
	OutputSchema map[string]any `json:"outputSchema,omitempty"`
	// Its mark where hosts list it: entrypoints show jm's (newMCPServer).
	Icons []map[string]any `json:"icons,omitempty"`
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
	server := &mcpServer{inflight: inflight{calls: map[string]runningCall{}}, watch: resourceWatch{stamps: map[string]string{}}, pollEvery: time.Second, home: gamesHome()}
	server.logLevel.Store(defaultLogLevel)
	for _, tools := range [][]mcpTool{server.gameTools(), server.makeTools(), server.playTools(), server.mentionTools(), server.sceneTools(), server.studioTools(), server.fileTools(), server.assetTools(), server.memoryTools(), server.worldTools(), server.playbookTools(), server.vcsTools(), server.variantTools(), server.sandboxTools(), server.taskTools(), server.editorTools(), settingsTools(), {pickTool(server), companionsTool()}, server.liveTools(), server.intelTools(), server.fileViewTools(), server.consoleTools()} {
		server.tools = append(server.tools, tools...)
	}
	for i, tool := range server.tools {
		if _, entry := tool.Meta["openai/ui"]; entry { // opened from the host's sidebar or tabs: jm's mark there
			server.tools[i].Icons = serverIcons()
		}
	}
	return server
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
	home      string         // the games folder, absolute: as the tools have it (the working folder moves)
	rootsWork sync.WaitGroup // roots answers being acted on
	byRecent  bool           // the open game is only the one opened last: the client's root beats it
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
		return getPrompt(get.Name, get.Arguments)
	case "resources/list":
		s.calls.Lock() // resources are the open game's: not while open_game switches it
		defer s.calls.Unlock()
		s.openRecentIfNone()
		if s.peer != nil {
			s.watch.listedNow()
		}
		return map[string]any{"resources": append(widgetResources(), projectResources()...)}, nil
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
		if content, ok := readWidget(read.URI); ok {
			return map[string]any{"contents": []map[string]any{content}}, nil
		}
		contents, ok, err := s.readGameResource(read.URI)
		if !ok {
			contents, ok, err = readMentioned(read.URI)
		}
		if ok {
			if err != nil {
				return nil, &rpcError{Code: -32002, Message: err.Error()}
			}
			return map[string]any{"contents": contents}, nil
		}
		text, mime, err := readResource(read.URI)
		if err != nil {
			return nil, &rpcError{Code: -32002, Message: err.Error()}
		}
		return map[string]any{"contents": []map[string]any{{"uri": read.URI, "mimeType": mime, "text": text}}}, nil
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
			if round != nil { // the game it runs in, now it's settled
				game, _ := os.Getwd()
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
			game, _ := os.Getwd()
			defer c.followEngineLog(game)()
			return tool.run(call.Arguments).reply(), nil
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

// toolArgs are a tool call's arguments, read leniently: a missing or
// mistyped one is its zero value, and the tool's default applies.
type toolArgs map[string]any

// resourcePath is the file a host's file entrypoint opened, as the host says
// in the call's _meta (a type JSON arguments can't make, so never the model's).
type resourcePath string

const resourceKey = "openai/resource"

// resource is the opened file's absolute path, "" when the call isn't from one.
func (a toolArgs) resource() string {
	p, _ := a[resourceKey].(resourcePath)
	return string(p)
}

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

// idempotent marks a tool whose second call with the same arguments changes nothing more.
func idempotent(annotations map[string]any) map[string]any {
	annotations["idempotentHint"] = true
	return annotations
}

// Schemas for structuredContent (outputSchema).
func typed(kind string) map[string]any { return map[string]any{"type": kind} }

// jmTool runs a jm command whose arguments args builds.
func jmTool(args func(a toolArgs) ([]string, error)) func(toolArgs) toolResult {
	return func(a toolArgs) toolResult {
		list, err := args(a)
		if err != nil {
			return textResult(err.Error(), true)
		}
		return textResult(runJM(a.call(), list...))
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

var exportTargetArg = strArg("platform as os-arch, e.g. windows-amd64 (default: this machine; others need that platform's engine as player)")

func exportArgs(a toolArgs) []string {
	return with(with([]string{"export"}, a.str("target") != "", "--target", a.str("target")), a.flag("server"), "--server")
}

func (s *mcpServer) makeTools() []mcpTool {
	return []mcpTool{
		{Name: "build", Description: "Call after you change the game's files, then play_verify. jm build --json: compile scripts, bake atlases, check scenes and prefabs. JSON lines; the last is the result.",
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
			Annotations: writes(), InputSchema: object(map[string]any{"target": exportTargetArg, "server": boolArg("export the dedicated multiplayer server instead")}),
			run: jmTool(func(a toolArgs) ([]string, error) { return exportArgs(a), nil })},
		{Name: "drive_start", Description: "For the person's own plays use play_show first. Start the built game under the stepped driver (headless; no window or GL unless gl is true). " +
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
		{Name: "drive", Description: "Driver commands, each answered as JSON: step [n], state [part...] [tag=Name...] [Component...] (e.g. state session tag=Player), get [tag=Name] <path> (get tag=Ball TransformComponent.x), " +
			"down|up|press <Key>, move x y, click [x y], wheel dy, set <key> <json>, scene <path>, marker [note] (marks the moment in a recorded run, as F8 does), " +
			"until [tag=Name] <path> <op> <value> [max n] (steps until it's true, e.g. until tag=Lift TransformComponent.y < -270), echo <text>, " +
			"near tag=Name [distance] (near misses: what's within distance, with the gaps), " +
			"debug physics on|off (colliders and terrain drawn over the frame), profile (as drive_profile), capture (the frame now, as an image; needs gl), quit. " +
			"To follow something frame by frame, send commands with repeat instead of a call per frame: " +
			"commands [\"step 1\", \"get tag=Player TransformComponent.y\"], repeat 40 (a reply line each). " +
			"A failing command stops the batch: the commands before it already ran, and the result is an error holding the replies so far. " +
			"Past 1 MB of replies the batch stops too (the last line says {\"truncated\":true,\"after\":n commands}): ask for less, e.g. get instead of state.",
			Annotations: writes(), InputSchema: object(map[string]any{
				"command":  strArg(`e.g. "step 60", "press Enter", "state"`),
				"commands": listArg("several commands in order, instead of command"),
				"repeat":   intArg("run commands this many times over (default 1, at most 1000)"),
			}),
			run: s.driving(func(a toolArgs) toolResult {
				commands := a.list("commands")
				if len(commands) == 0 && a.str("command") != "" {
					commands = []string{a.str("command")}
				}
				if len(commands) == 0 {
					return textResult(`give a command, e.g. "step 60", or commands`, true)
				}
				return s.driveBatch(a.call(), commands, int(a.num("repeat", 1, 1, 1000)), driveReplyLimit)
			})},
		{Name: "drive_frame", Title: "See the driven game", Description: "An image of the driven game as it is now (the last frame it ran). " +
			"Needs a game started with gl: true. Use it to see what your commands did, e.g. after step 60 or a click.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}), run: s.driving(s.driveFrame)},
		{Name: "drive_profile", Title: "Where the driven game's time goes", Description: "Call when the game feels slow or you changed something costly: " +
			"where the driven game's last frames (up to 120) went: each system's, script's and module's average and worst ms, " +
			"and counts (entities, scripts, draw calls, sprites). Step to the moment you care about first. " +
			"Without gl there's no GPU work, so the CPU's share is what it measures.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}), run: s.driving(s.driveProfile)},
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

// driving is a tool on the driven game, which its call's cancel kills, mid-step or not.
func (s *mcpServer) driving(run func(toolArgs) toolResult) func(toolArgs) toolResult {
	return func(a toolArgs) toolResult {
		release := func() bool { return false }
		if s.game != nil {
			release = s.game.stopOn(a.call().context())
		}
		result := run(a)
		if release(); a.call().context().Err() != nil {
			s.stopDriver() // killed (or about to be): reaped, its folder gone
		}
		return result
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
	cmd := jmCommand(a.call().context(), "", args...)
	cmd.Env = append(os.Environ(), "JM_RENDERER=none", "JM_REALTIME=1", fmt.Sprintf("JM_EXIT_AFTER_FRAMES=%d", frames),
		"JM_DUMP_DIR="+filepath.Join(work, "dump"), "JM_SAVE_DIR="+filepath.Join(work, "save"),
		"JM_ERRORS="+filepath.Join(work, "errors.jsonl"))
	if len(replays) > 0 {
		cmd.Env = append(cmd.Env, "JM_INPUT_REPLAY="+filepath.Join(work, "{peer}.txt"))
	}
	start := time.Now() // it runs in real time, 60 frames a second: the clock says how far it got
	stop := every(time.Second, func() {
		f := min(int(time.Since(start).Seconds()*60), frames)
		a.call().progress(float64(f), float64(frames), fmt.Sprintf("Frame %d of %d", f, frames))
	})
	output, runErr := a.call().run(cmd)
	stop()

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

// runJM runs this jm with args in the current folder for call c: its output,
// and whether it failed. A variable so tests can see what a tool runs.
var runJM = func(c *toolCall, args ...string) (string, bool) { return jmIn(c, "", args...) }

// jmIn runs this jm with args in dir ("": the current folder) for call c.
func jmIn(c *toolCall, dir string, args ...string) (string, bool) {
	out, err := c.run(jmCommand(c.context(), dir, args...))
	text := strings.TrimSpace(string(out))
	if err != nil && text == "" {
		text = err.Error()
	}
	return text, err != nil
}

// jmCommand is this jm with args, run in dir until ctx ends.
func jmCommand(ctx context.Context, dir string, args ...string) *exec.Cmd {
	self, err := os.Executable()
	if err != nil {
		self = "jm"
	}
	cmd := exec.CommandContext(ctx, self, args...)
	cmd.Dir = dir
	return cmd
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
		if out, failed := runJM(a.call(), "build", "--json"); failed {
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
	o.Stop = a.call().context() // while it starts and replays: then it outlives the call
	s.game, err = startGame(root, o)
	if err != nil {
		return textResult(err.Error(), true)
	}
	if o.Play != nil {
		// Replay the play up to the moment: the game is then where the person was.
		err = s.game.runTo(a.call(), from, 0, float64(from+1))
	}
	if s.game.unwatch(); err == nil {
		err = context.Cause(o.Stop) // cancelled as it got ready
	}
	if err != nil {
		s.stopDriver()
		return textResult(err.Error(), true)
	}
	return jsonResult(map[string]any{"ok": true, "ready": true, "frame": s.game.frame, "recording": o.Record})
}

// driveCommand sends one command to the driven game: its reply line, the
// frame's image for capture, and whether it failed (the game refused it, or ended).
func (s *mcpServer) driveCommand(command string) (string, *mcpImage, bool) {
	if s.game == nil {
		return "no game running: call drive_start first", nil, true
	}
	verb := strings.Fields(command)
	switch {
	case strings.ContainsAny(command, "\n\r"):
		return "one command per call", nil, true
	case len(verb) == 0 || strings.HasPrefix(verb[0], "#"):
		return `give a command, e.g. "step 60" (a blank or # line gets no answer)`, nil, true
	case verb[0] == "capture":
		// Into the driver's folder, whatever path it names: the engine writes wherever it's told.
		path, frame, err := s.captureFrame()
		if err != nil {
			return err.Error(), nil, true
		}
		data := jpegOf(path, 960)
		if data == nil {
			return "couldn't read the image " + path, nil, true
		}
		return fmt.Sprintf(`{"ok":true,"frame":%d,"image":"attached"}`, frame), &mcpImage{data, "image/jpeg"}, false
	}
	line, err := s.game.send(command)
	if err != nil {
		return s.ended(err.Error()), nil, true
	}
	if verb[0] == "quit" {
		return s.ended(line), nil, false
	}
	var reply struct {
		OK bool `json:"ok"`
	}
	_ = json.Unmarshal([]byte(line), &reply)
	return line, nil, !reply.OK
}

// driveReplyLimit keeps a batch's replies to what an agent's context can take.
const driveReplyLimit = 1 << 20

// driveBatch runs commands repeat times over, a reply line each. It stops at
// the first failure (the rest would run on from somewhere unexpected) and once
// the replies, images included, pass limit bytes, saying so in a last line.
func (s *mcpServer) driveBatch(call *toolCall, commands []string, repeat, limit int) toolResult {
	var replies []string
	var images []mcpImage
	result := func(failed bool) toolResult {
		return toolResult{Text: strings.Join(replies, "\n"), Images: images, Failed: failed}
	}
	size, ran := 0, 0
	for range repeat {
		for _, c := range commands {
			if size > limit {
				replies = append(replies, fmt.Sprintf(`{"truncated":true,"after":%d}`, ran))
				return result(false)
			}
			call.progress(float64(ran+1), float64(repeat*len(commands)), c)
			reply, img, failed := s.driveCommand(c)
			replies = append(replies, reply)
			size += len(reply) + 1
			if img != nil {
				images = append(images, *img)
				size += base64.StdEncoding.EncodedLen(len(img.Data))
			}
			ran++
			if failed {
				return result(true)
			}
		}
	}
	return result(false)
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
	path, frame, err := s.captureFrame()
	if err != nil {
		return textResult(err.Error(), true)
	}
	return imageResult(path, fmt.Sprintf("Frame %d of the driven game.", frame), map[string]any{"frame": frame})
}

// captureFrame saves the driven game's last frame run: its image file and number.
func (s *mcpServer) captureFrame() (string, uint64, error) {
	path, err := s.game.capture()
	if err != nil && strings.Contains(err.Error(), "JM_RENDERER=none") {
		err = fmt.Errorf("this game was started without GL: drive_start with gl: true to see it (state has the draw list without)")
	}
	return path, s.game.frame - min(s.game.frame, 1), err
}

func (s *mcpServer) driveProfile(toolArgs) toolResult {
	if s.game == nil {
		return textResult("no game running: call drive_start first, then step", true)
	}
	reply, err := s.game.do("profile")
	if err != nil {
		return textResult(err.Error(), true)
	}
	var profile map[string]any
	if err := json.Unmarshal(reply["profile"], &profile); err != nil {
		return textResult(err.Error(), true)
	}
	return jsonResult(profile)
}

// projectResources lists the files an agent reads and writes, and the schema.
func projectResources() []map[string]any {
	resources := []map[string]any{{"uri": "jm://schema", "name": "schema", "mimeType": "application/json",
		"description": "every component's scene keys and script fields (jm schema)"},
		{"uri": "jm://memory", "name": "memory", "mimeType": "application/json", "description": "the game's design notes (design/): each one's title, observations and links"}}
	for _, t := range docs.Topics() {
		resources = append(resources, map[string]any{"uri": "jm://docs/" + t.Name, "name": "docs/" + t.Name,
			"mimeType": "text/markdown", "description": t.Title})
	}
	for _, f := range gameFiles(func(path string) bool { return mimeOf(path) != "" }) {
		abs, _ := filepath.Abs(f)
		resources = append(resources, map[string]any{"uri": fileURI(abs), "name": f, "mimeType": mimeOf(f)})
	}
	return resources
}

// agentFile is projectFile's path, unless it's in a folder hidden from agents:
// a file listed or read for them, so a link into build/ is neither.
func agentFile(path string) (string, error) {
	rel, err := projectFile(path)
	if err == nil && hiddenFromAgents(filepath.Dir(rel)) {
		return "", fmt.Errorf("%s isn't one of the project's files", path)
	}
	return rel, err
}

// hiddenFromAgents says whether a folder of the project (a path from its
// folder) is one resources leave out: dot-folders (.git, .jm), build output,
// node_modules, dist.
func hiddenFromAgents(dir string) bool {
	for _, part := range strings.Split(filepath.ToSlash(dir), "/") {
		lower := strings.ToLower(part) // macOS's and Windows' disks don't tell BUILD from build
		if part != "." && (strings.HasPrefix(part, ".") || lower == "build" || lower == "node_modules" || lower == "dist") {
			return true
		}
	}
	return false
}

// fileURI is the file:// URI of an absolute path, each segment escaped
// ("a b#2.ts": "a%20b%232.ts"); readResource unescapes it.
// gameFiles are the open game's files that keep says to, as sorted slash
// paths from its folder (not build outputs or what agents shouldn't see).
func gameFiles(keep func(path string) bool) []string {
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
		if keep(path) {
			files = append(files, filepath.ToSlash(path))
		}
		return nil
	})
	sort.Strings(files)
	return files
}

func fileURI(abs string) string {
	segments := strings.Split(filepath.ToSlash(abs), "/")
	for i, s := range segments {
		segments[i] = url.PathEscape(s)
	}
	return "file://" + strings.Join(segments, "/")
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
		out, failed := runJM(nil, "schema")
		if failed {
			return "", "", fmt.Errorf("%s", out)
		}
		return out, "application/json", nil
	}
	if uri == "jm://memory" {
		data, err := json.Marshal(map[string]any{"notes": memory.Notes(".")})
		return string(data), "application/json", err
	}
	if topic, ok := strings.CutPrefix(uri, "jm://docs/"); ok {
		text, err := docs.Read(topic)
		return text, "text/markdown", err
	}
	path, ok := strings.CutPrefix(uri, "file://")
	if !ok {
		return "", "", fmt.Errorf("unknown resource %s", uri)
	}
	path, err := url.PathUnescape(path)
	if err != nil {
		return "", "", err
	}
	// The files projectResources lists, and no others.
	rel, err := agentFile(path)
	if err != nil {
		return "", "", err
	}
	if mimeOf(rel) == "" {
		return "", "", fmt.Errorf("%s isn't one of the project's resources", uri)
	}
	data, err := os.ReadFile(rel)
	if err != nil {
		return "", "", err
	}
	return string(data), mimeOf(path), nil
}
