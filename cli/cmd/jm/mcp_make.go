package main

import (
	"cmp"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

// The tools that run a jm command in a game: build, test, golden, schema, fmt, export, session…

// jmTool is a tool on a game that runs a jm command in it, its arguments built from the call.
func jmTool(args func(root string, a toolArgs) ([]string, error)) func(root string, a toolArgs) toolResult {
	return func(root string, a toolArgs) toolResult {
		list, err := args(root, a)
		if err != nil {
			return textResult(err.Error(), true)
		}
		return textResult(runJM(a.call(), root, list...))
	}
}

var exportTargetArg = strArg("platform as os-arch, e.g. windows-amd64 (default: this machine; others need that platform's engine as player)")

func exportArgs(a toolArgs) []string {
	return with(with([]string{"export"}, a.str("target") != "", "--target", a.str("target")), a.flag("server"), "--server")
}

func (s *mcpServer) makeTools() []mcpTool {
	return []mcpTool{
		{Name: "build", Description: "Call after you change the game's files, then play_verify. jm build --json: compile scripts, bake atlases, check scenes and prefabs. JSON lines; the last is the result.",
			Annotations: writes(), InputSchema: object(map[string]any{}),
			onGame: jmTool(func(string, toolArgs) ([]string, error) { return []string{"build", "--json"}, nil })},
		{Name: "doctor", Description: "jm doctor --json: jm's and the engine's versions, the script toolchain (Node, AssemblyScript), the project, and any problems with their fixes.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}),
			orNone: true, onGame: jmTool(func(string, toolArgs) ([]string, error) { return []string{"doctor", "--json"}, nil })},
		{Name: "test", Description: "jm test --json: run tests/*.spec.ts (game logic, no engine). A JSON line per test; the last is the result.",
			Annotations: writes(), InputSchema: object(map[string]any{"specs": listArg("spec files (default: all)")}),
			onGame: jmTool(func(root string, a toolArgs) ([]string, error) {
				specs, err := gamePaths(root, a.list("specs"))
				return positional([]string{"test", "--json"}, specs...), err
			})},
		{Name: "golden", Description: "jm golden --json: compare frames with tests/golden images (update: record them instead). Build first.",
			Annotations: writes(), InputSchema: object(map[string]any{"names": listArg("goldens to check (default: all)"), "update": boolArg("record the frames as the new goldens")}),
			onGame: jmTool(func(_ string, a toolArgs) ([]string, error) {
				return positional(with([]string{"golden", "--json"}, a.flag("update"), "--update"), a.list("names")...), nil
			})},
		{Name: "schema", Description: "jm schema: every component's scene keys and script fields (or one component's), as JSON.",
			Annotations: readOnly(), InputSchema: object(map[string]any{"component": strArg("e.g. SpriteComponent (default: all)")}),
			orNone: true, onGame: jmTool(func(_ string, a toolArgs) ([]string, error) { // the game's script fields too
				if c := a.str("component"); c != "" {
					return positional([]string{"schema"}, c), nil
				}
				return []string{"schema"}, nil
			})},
		{Name: "generate", Description: "jm generate <kind> <name>: make a file from a template (jm generate list shows the kinds).",
			Annotations: writes(), InputSchema: object(map[string]any{"kind": strArg("e.g. prefab, script, scene, ui, shader, bindings, list"), "name": strArg("the new file's name")}, "kind"),
			onGame: jmTool(func(_ string, a toolArgs) ([]string, error) {
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
			onGame: jmTool(func(root string, a toolArgs) ([]string, error) {
				files, err := gamePaths(root, a.list("files"))
				return positional(with([]string{"fmt"}, a.flag("check"), "--check"), files...), err
			})},
		{Name: "export", Description: "jm export: build the game and write a standalone executable with everything inside (an .app on macOS) to dist/, for the person to run or share.",
			Annotations: writes(), InputSchema: object(map[string]any{"target": exportTargetArg, "server": boolArg("export the dedicated multiplayer server instead")}),
			onGame: jmTool(func(_ string, a toolArgs) ([]string, error) { return exportArgs(a), nil })},
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
			onGame: runSessionTool},
	}
}

// runSessionTool is the "session" tool: jm run --peers with every peer's
// final state dumped, then those dumps, trimmed to what a session is about.
func runSessionTool(root string, a toolArgs) toolResult {
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
	cmd := jmCommand(a.call().context(), root, args...)
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

// runJM runs this jm with args in the game dir ("": none) for call c: its
// output, and whether it failed. A variable so tests can see what a tool runs where.
var runJM = func(c *toolCall, dir string, args ...string) (string, bool) { return jmIn(c, dir, args...) }

// jmIn runs this jm with args in the game dir ("": none) for call c.
func jmIn(c *toolCall, dir string, args ...string) (string, bool) {
	out, err := c.run(jmCommand(c.context(), dir, args...))
	text := strings.TrimSpace(string(out))
	if err != nil && text == "" {
		text = err.Error()
	}
	return text, err != nil
}

// jmCommand is this jm with args, run in the game dir ("": none, so where no game is) until ctx ends.
func jmCommand(ctx context.Context, dir string, args ...string) *exec.Cmd {
	self, err := os.Executable()
	if err != nil {
		self = "jm"
	}
	cmd := exec.CommandContext(ctx, self, args...)
	cmd.Dir = cmp.Or(dir, os.TempDir())
	return cmd
}
