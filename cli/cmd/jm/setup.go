package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"

	"github.com/BurntSushi/toml"
	"github.com/spf13/cobra"
)

var setupCmd = &cobra.Command{
	Use:   "setup [agent...]",
	Short: "Connect agent apps to jm (adds jm's MCP server to their settings)",
	Long: `Adds jm's MCP server ("jm mcp") to agent apps' settings, so they can build,
test and play your games. Agents: claude-code, claude-desktop, codex, chatgpt.
Without one, it sets up every agent app it finds on this machine.

Running it again is safe: it replaces its own entry (named "journeyman") and
leaves the app's other settings alone. Agent sessions already open get the
tools when they restart.
ChatGPT reaches MCP servers over the internet, so for it setup prints the steps.`,
	RunE: func(cmd *cobra.Command, args []string) error {
		jm, err := executablePath()
		if err != nil {
			return err
		}
		if strings.HasPrefix(jm, os.TempDir()) || strings.Contains(jm, "go-build") {
			return fmt.Errorf("this jm (%s) is temporary: run setup from an installed jm, or apps lose it", jm)
		}
		return setupAgents(args, jm, cmd.OutOrStdout())
	},
}

// agentApp is an agent app jm can add itself to.
type agentApp struct {
	name      string
	found     func() bool
	add       func(jm string) (string, error) // says what it did
	connected func() bool                     // has a journeyman server (read from its settings, quickly)
}

var agentApps = []agentApp{
	{"claude-code", hasCommand("claude"), addToClaudeCode, claudeCodeConnected},
	{"claude-desktop", func() bool { return claudeDesktopConfig() != "" && exists(filepath.Dir(claudeDesktopConfig())) }, addToClaudeDesktop,
		func() bool { return hasServer(claudeDesktopConfig()) }},
	{"codex", func() bool { return exists(filepath.Join(codexHome(), "config.toml")) || hasCommand("codex")() }, addToCodex, func() bool {
		var config struct {
			Servers map[string]any `toml:"mcp_servers"`
		}
		_, err := toml.DecodeFile(filepath.Join(codexHome(), "config.toml"), &config)
		return err == nil && config.Servers["journeyman"] != nil
	}},
	{"chatgpt", func() bool { return false }, chatGPTSteps, func() bool { return false }},
}

// hasServer says whether a JSON settings file (Claude's) has mcpServers.journeyman.
func hasServer(path string) bool {
	var config struct {
		Servers map[string]any `json:"mcpServers"`
	}
	data, err := os.ReadFile(path)
	return err == nil && json.Unmarshal(data, &config) == nil && config.Servers["journeyman"] != nil
}

// claudeCodeConnected checks Claude Code's three scopes: user, this project
// (local, in ~/.claude.json), and the project's shared .mcp.json.
func claudeCodeConnected() bool {
	path := filepath.Join(homeDir(), ".claude.json")
	if hasServer(path) || hasServer(".mcp.json") {
		return true
	}
	var config struct {
		Projects map[string]struct {
			Servers map[string]any `json:"mcpServers"`
		} `json:"projects"`
	}
	data, _ := os.ReadFile(path)
	cwd, _ := os.Getwd()
	return json.Unmarshal(data, &config) == nil && config.Projects[cwd].Servers["journeyman"] != nil
}

// agentState is whether an agent app here has jm; thisSession marks the app
// jm runs inside (it may not be "found": Claude Code with no claude on PATH).
type agentState struct {
	Name        string `json:"name"`
	Connected   bool   `json:"connected"`
	ThisSession bool   `json:"thisSession,omitempty"`
}

func agentStates() []agentState {
	var states []agentState
	for _, app := range agentApps {
		if here := app.name == sessionAgent(); here || app.found() {
			states = append(states, agentState{app.name, app.connected(), here})
		}
	}
	return states
}

// sessionAgent is the agent app jm was started from, by the variables each
// sets for its commands, or "".
func sessionAgent() string {
	switch {
	case os.Getenv("CLAUDECODE") == "1":
		return "claude-code"
	case os.Getenv("CODEX_THREAD_ID") != "":
		return "codex"
	}
	return ""
}

func setupAgents(names []string, jm string, out io.Writer) error {
	var chosen []agentApp
	for _, name := range names {
		app, ok := findAgentApp(name)
		if !ok {
			return fmt.Errorf("no agent called %q: there are claude-code, claude-desktop, codex and chatgpt", name)
		}
		chosen = append(chosen, app)
	}
	if len(chosen) == 0 {
		for _, app := range agentApps {
			if app.found() || app.name == sessionAgent() { // as doctor lists them
				chosen = append(chosen, app)
			}
		}
		if len(chosen) == 0 {
			return fmt.Errorf("found no agent apps (Claude Code, Claude Desktop, Codex); name one to set it up anyway: jm setup codex")
		}
	}
	var failed []string
	blocked, added := false, false
	for _, app := range chosen {
		did, err := app.add(jm)
		if err != nil {
			failed = append(failed, app.name)
			fmt.Fprintf(out, "%-15s failed: %v\n", app.name, err)
			if sandboxBlocked(err) {
				blocked = true
				fmt.Fprintf(out, "%-15s your sandbox blocks writing this app's settings (outside your workspace)\n", "")
			}
			continue
		}
		added = added || app.name != "chatgpt"
		fmt.Fprintf(out, "%-15s %s\n", app.name, did)
	}
	if added {
		where := "the game's folder"
		if cwd, err := os.Getwd(); err == nil && exists(filepath.Join(cwd, archive.ManifestEntryKey)) {
			where = cwd
		}
		fmt.Fprintf(out, "\nSessions already open don't have jm's tools yet: restart this session in %s\n"+
			"to get them (or call open_game then). Until then, use the jm command.\n", where)
	}
	switch {
	case blocked:
		return fmt.Errorf("not set up: %s. A sandbox blocked it: run `jm setup` again with escalated permissions "+
			"(ask the person to approve it), or ask them to run `jm setup` in a terminal", strings.Join(failed, ", "))
	case len(failed) > 0:
		return fmt.Errorf("not set up: %s (why is above)", strings.Join(failed, ", "))
	}
	return nil
}

// sandboxBlocked says whether a settings write failed for want of permission,
// as under Codex's sandbox (which protects ~/.codex even in a writable root).
func sandboxBlocked(err error) bool {
	text := strings.ToLower(err.Error())
	return errors.Is(err, fs.ErrPermission) || strings.Contains(text, "operation not permitted") ||
		strings.Contains(text, "permission denied") || strings.Contains(text, "read-only file system")
}

func findAgentApp(name string) (agentApp, bool) {
	for _, app := range agentApps {
		if app.name == name {
			return app, true
		}
	}
	return agentApp{}, false
}

func hasCommand(name string) func() bool {
	return func() bool { _, err := exec.LookPath(name); return err == nil }
}

// Claude Code keeps its settings in a file it rewrites itself: go through its
// CLI. A "journeyman" that isn't a jm is someone else's, so it's left alone.
func addToClaudeCode(jm string) (string, error) {
	if !hasCommand("claude")() { // a session's own app, its CLI elsewhere
		return "", fmt.Errorf("claude isn't on PATH here: run `claude mcp add --scope user journeyman -- %s mcp` where it is", shellQuote(jm))
	}
	if out, err := exec.Command("claude", "mcp", "get", "journeyman").CombinedOutput(); err == nil {
		if !runsJM(string(out)) {
			return "", fmt.Errorf("Claude Code already has a server called journeyman that isn't jm: %s", strings.TrimSpace(string(out)))
		}
		if out, err := exec.Command("claude", "mcp", "remove", "--scope", "user", "journeyman").CombinedOutput(); err != nil {
			return "", fmt.Errorf("claude mcp remove: %v: %s", err, strings.TrimSpace(string(out)))
		}
	}
	if out, err := exec.Command("claude", "mcp", "add", "--scope", "user", "journeyman", "--", jm, "mcp").CombinedOutput(); err != nil {
		return "", fmt.Errorf("claude mcp add: %v: %s", err, strings.TrimSpace(string(out)))
	}
	return "added journeyman (every project; it uses the game in the folder Claude Code runs in)", nil
}

// runsJM says whether `claude mcp get` describes a server whose command is a jm.
func runsJM(description string) bool {
	for _, line := range strings.Split(description, "\n") {
		if command, ok := strings.CutPrefix(strings.TrimSpace(line), "Command:"); ok {
			name := strings.TrimSuffix(filepath.Base(strings.TrimSpace(command)), ".exe")
			return name == "jm"
		}
	}
	return false
}

func claudeDesktopConfig() string {
	switch runtime.GOOS {
	case "darwin":
		return filepath.Join(homeDir(), "Library", "Application Support", "Claude", "claude_desktop_config.json")
	case "windows":
		if os.Getenv("APPDATA") == "" {
			return "" // no roaming folder: nowhere to look
		}
		return filepath.Join(os.Getenv("APPDATA"), "Claude", "claude_desktop_config.json")
	}
	return filepath.Join(homeDir(), ".config", "Claude", "claude_desktop_config.json")
}

func addToClaudeDesktop(jm string) (string, error) {
	path := claudeDesktopConfig()
	if path == "" {
		return "", fmt.Errorf("APPDATA isn't set, so there's no Claude Desktop config to find")
	}
	config := map[string]any{}
	data, err := os.ReadFile(path)
	if err != nil && !os.IsNotExist(err) {
		return "", err
	}
	if len(strings.TrimSpace(string(data))) > 0 {
		if err := json.Unmarshal(data, &config); err != nil || config == nil {
			return "", fmt.Errorf("%s isn't a JSON object: fix or remove it first", path)
		}
	}
	servers, ok := config["mcpServers"].(map[string]any)
	if config["mcpServers"] == nil {
		servers, ok = map[string]any{}, true
	}
	if !ok {
		return "", fmt.Errorf(`%s has an "mcpServers" that isn't an object: fix it first`, path)
	}
	servers["journeyman"] = map[string]any{"command": jm, "args": []string{"mcp"}}
	config["mcpServers"] = servers
	data, _ = json.MarshalIndent(config, "", "  ")
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return "", err
	}
	if err := atomicfile.WriteFile(path, append(data, '\n'), 0o644); err != nil {
		return "", err
	}
	return "added journeyman to " + path + " (restart Claude Desktop)", nil
}

func codexHome() string {
	if dir := os.Getenv("CODEX_HOME"); dir != "" {
		return dir
	}
	return filepath.Join(homeDir(), ".codex")
}

// addToCodex adds jm to Codex's config.toml (shared by the Codex CLI and app):
// through the codex CLI when there is one, else by writing the
// [mcp_servers.journeyman] table, but only where that edit is surely safe.
func addToCodex(jm string) (string, error) {
	path := filepath.Join(codexHome(), "config.toml")
	if hasCommand("codex")() {
		_ = exec.Command("codex", "mcp", "remove", "journeyman").Run() // absent is fine
		if out, err := exec.Command("codex", "mcp", "add", "journeyman", "--", jm, "mcp").CombinedOutput(); err != nil {
			return "", fmt.Errorf("codex mcp add: %v: %s", err, strings.TrimSpace(string(out)))
		}
		return "added journeyman to " + path + " (restart the Codex app; the CLI picks it up next run)", nil
	}
	old, err := os.ReadFile(path)
	if err != nil && !os.IsNotExist(err) {
		return "", err
	}
	entry := codexEntry(jm)
	// Only whole tables jm wrote are removed, and only appended to where nothing could clash.
	whole := true
	for _, m := range codexEntryPattern.FindAllStringIndex(string(old), -1) {
		whole = whole && codexTableEnds(string(old[m[1]:]))
	}
	rest := codexEntryPattern.ReplaceAllString(string(old), "")
	if !whole || codexUnsafe.MatchString(rest) {
		return "", fmt.Errorf("%s is more than jm can safely edit; add this to it by hand:\n%s", path, entry)
	}
	text := strings.TrimRight(rest, "\n")
	if text != "" {
		text += "\n\n"
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return "", err
	}
	if err := atomicfile.WriteFile(path, []byte(text+entry), 0o644); err != nil {
		return "", err
	}
	return "added journeyman to " + path + " (restart the Codex app; the CLI picks it up next run)", nil
}

// codexEntry is the table jm writes; codexEntryPattern finds any it wrote
// before (any jm path), and codexUnsafe what makes appending one unsafe: a
// journeyman entry jm didn't write, an inline or dotted mcp_servers key, a key or
// header with escapes, or multi-line strings (which could hold anything).
func codexEntry(jm string) string {
	return fmt.Sprintf("[mcp_servers.journeyman]\ncommand = %s\nargs = [\"mcp\"]\n", tomlString(jm))
}

var (
	codexEntryPattern = regexp.MustCompile(`(?m)^\[mcp_servers\.journeyman\]\r?\ncommand = "(?:[^"\\\r\n]|\\.)*"\r?\nargs = \["mcp"\]\r?\n?`)
	codexUnsafe       = regexp.MustCompile(`(?m)^\s*\[[^\n]*(journeyman|\\)|journeyman\s*=|^\s*["']?(mcp_servers|journeyman)["']?\s*[.=]|^[^=#\n]*\\|"""|'''`)
)

// codexTableEnds says whether the text after a table jm wrote holds no more of its
// keys: only blank lines and comments before the next header.
func codexTableEnds(after string) bool {
	for _, line := range strings.Split(after, "\n") {
		line = strings.TrimSpace(line)
		if line != "" && !strings.HasPrefix(line, "#") {
			return strings.HasPrefix(line, "[")
		}
	}
	return true
}

// tomlString quotes s as a TOML basic string (backslashes in Windows paths too).
func tomlString(s string) string {
	quoted, _ := json.Marshal(s) // JSON's escapes are valid TOML basic-string escapes
	return string(quoted)
}

func chatGPTSteps(string) (string, error) {
	return `needs a public URL, so it's by hand:
                1. jm mcp --http 127.0.0.1:8787 --dir <your game>  (prints the URL path to use)
                2. expose it with a tunnel (e.g. cloudflared tunnel --url http://127.0.0.1:8787)
                3. ChatGPT: Settings > Connectors > Developer mode, add the tunnel URL + that path`, nil
}

func homeDir() string {
	home, _ := os.UserHomeDir()
	return home
}
