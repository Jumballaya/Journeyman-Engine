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
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"

	"github.com/BurntSushi/toml"
	"github.com/spf13/cobra"
)

var setupRemove bool

var setupCmd = &cobra.Command{
	Use:   "setup [agent...]",
	Short: "Connect agent apps to jm (adds jm's MCP server to their settings)",
	Long: `Adds jm's MCP server ("jm mcp") to agent apps' settings, so they can build,
test and play your games. Agents: claude-code, claude-desktop, codex, chatgpt.
Without one, it sets up every agent app it finds on this machine.

Running it again is safe: it replaces its own entry (named "journeyman") and
leaves the app's other settings alone. Agent sessions already open get the
tools when they restart.
ChatGPT Desktop reads its MCP servers from Codex's settings (~/.codex/config.toml),
so setting up either sets up both. ChatGPT on the web reaches MCP servers over
the internet: without ChatGPT Desktop, setup chatgpt prints the steps.

--remove takes jm's entry out again (without agents: from every app that has
one); an entry called journeyman that isn't a jm stays.`,
	RunE: func(cmd *cobra.Command, args []string) error {
		if setupRemove {
			return setupAgents(args, "", true, cmd.OutOrStdout())
		}
		jm, err := executablePath()
		if err != nil {
			return err
		}
		if strings.HasPrefix(jm, os.TempDir()) || strings.Contains(jm, "go-build") {
			return fmt.Errorf("this jm (%s) is temporary: run setup from an installed jm, or apps lose it", jm)
		}
		return setupAgents(args, jm, false, cmd.OutOrStdout())
	},
}

func init() {
	setupCmd.Flags().BoolVar(&setupRemove, "remove", false, "take jm out of the agent apps instead")
}

// agentApp is an agent app jm can add itself to.
type agentApp struct {
	name      string
	title     string // as the app calls itself
	found     func() bool
	add       func(jm string) (string, error) // add and remove say what they did
	remove    func() (string, error)
	server    func() (command string, ok bool) // its journeyman server where setup adds it (read quickly)
	elsewhere func() string                    // how to remove journeyman servers setup didn't add, or ""; nil: there are none
}

var agentApps = []agentApp{
	{"claude-code", "Claude Code", hasCommand("claude"), addToClaudeCode, removeFromClaudeCode,
		func() (string, bool) { return jsonServer(claudeCodeConfig()) }, claudeCodeElsewhere},
	{"claude-desktop", "Claude Desktop", func() bool { return claudeDesktopConfig() != "" && exists(filepath.Dir(claudeDesktopConfig())) },
		addToClaudeDesktop, removeFromClaudeDesktop, func() (string, bool) { return jsonServer(claudeDesktopConfig()) }, nil},
	{"codex", "Codex", func() bool { return exists(filepath.Join(codexHome(), "config.toml")) || hasCommand("codex")() },
		addToCodex, removeFromCodex, codexServer, nil},
	{"chatgpt", "ChatGPT", chatGPTDesktopFound, addToChatGPT, removeFromChatGPT, chatGPTServer, nil},
}

// connected says whether the app has jm's server in any scope, setup's or another.
func (app agentApp) connected() bool {
	_, ok := app.server()
	return ok || app.elsewhere != nil && app.elsewhere() != ""
}

// jsonServer is the command of mcpServers.journeyman in a JSON settings file
// (Claude's), if it has one.
func jsonServer(path string) (string, bool) {
	var config struct {
		Servers map[string]*struct {
			Command string `json:"command"`
		} `json:"mcpServers"`
	}
	data, err := os.ReadFile(path)
	if err != nil || json.Unmarshal(data, &config) != nil || config.Servers["journeyman"] == nil {
		return "", false
	}
	return config.Servers["journeyman"].Command, true
}

func hasServer(path string) bool { _, ok := jsonServer(path); return ok }

// claudeCodeElsewhere checks the Claude Code scopes setup doesn't write: this
// folder's local one (in ~/.claude.json) and its shared .mcp.json.
func claudeCodeElsewhere() string {
	var config struct {
		Projects map[string]struct {
			Servers map[string]any `json:"mcpServers"`
		} `json:"projects"`
	}
	data, _ := os.ReadFile(claudeCodeConfig())
	cwd, _ := os.Getwd()
	var steps []string
	if json.Unmarshal(data, &config) == nil && config.Projects[cwd].Servers["journeyman"] != nil {
		steps = append(steps, "run `claude mcp remove --scope local journeyman` in "+cwd)
	}
	if hasServer(".mcp.json") {
		steps = append(steps, "take journeyman out of "+filepath.Join(cwd, ".mcp.json")+" (shared with the project)")
	}
	if len(steps) == 0 {
		return ""
	}
	return "Connected through a journeyman entry jm setup didn't add, so it can't remove it: " + strings.Join(steps, ", and ")
}

func claudeCodeConfig() string { return filepath.Join(homeDir(), ".claude.json") }

// agentState is an agent app and whether it has jm; thisSession marks the app
// jm runs inside (it may not be found: Claude Code with no claude on PATH).
type agentState struct {
	Name        string `json:"name"`
	Title       string `json:"title"`
	Status      string `json:"status"` // connected, not-connected, not-installed, manual (ChatGPT: jm can't tell)
	ThisSession bool   `json:"thisSession,omitempty"`
	Removable   bool   `json:"removable,omitempty"` // jm setup --remove can take it out
	Elsewhere   string `json:"elsewhere,omitempty"` // how to remove what it can't
}

// agentStates lists every agent app jm knows.
func agentStates() []agentState {
	var states []agentState
	for _, app := range agentApps {
		state := agentState{Name: app.name, Title: app.title, Status: "not-installed", ThisSession: app.name == sessionAgent()}
		command, ok := app.server()
		state.Removable = ok && isJM(command)
		if app.elsewhere != nil {
			state.Elsewhere = app.elsewhere()
		}
		switch {
		case app.connected():
			state.Status = "connected"
		case state.ThisSession || app.found():
			state.Status = "not-connected"
		case app.name == "chatgpt": // on the web
			state.Status = "manual"
		}
		states = append(states, state)
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

// unconnectedSession is the agent app jm runs in when that app lacks jm's MCP server, else "".
func unconnectedSession() string {
	app, ok := findAgentApp(sessionAgent())
	if !ok || app.connected() {
		return ""
	}
	return app.name
}

// connectNudge tells an agent running in app how to get jm's tools.
func connectNudge(app string) string {
	return app + " isn't connected to jm, so this session can't see the person's plays or the game's frames: " +
		"run `jm setup` (ask the person to approve it if your sandbox blocks it), then ask them to restart " +
		"this session in the game's folder. Until then, jm in the shell works."
}

// setupAgents adds jm (or with remove, takes it out) of the named apps, or of
// every app it applies to: found ones to add to, connected ones to remove from.
func setupAgents(names []string, jm string, remove bool, out io.Writer) error {
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
			if remove && app.connected() || !remove && (app.found() || app.name == sessionAgent()) { // as doctor lists them
				chosen = append(chosen, app)
			}
		}
		if len(chosen) == 0 && remove {
			fmt.Fprintln(out, "no agent app here has jm: nothing to remove")
			return nil
		}
		if len(chosen) == 0 {
			return fmt.Errorf("found no agent apps (Claude Code, Claude Desktop, Codex, ChatGPT Desktop); name one to set it up anyway: jm setup codex")
		}
	}
	var failed []string
	blocked := false
	var changed []string // titles
	connectedBefore := map[string]bool{}
	for _, app := range agentApps {
		connectedBefore[app.name] = app.connected()
	}
	for _, app := range chosen {
		var did string
		var err error
		if remove {
			did, err = app.remove()
		} else {
			did, err = app.add(jm)
		}
		if err != nil {
			failed = append(failed, app.name)
			fmt.Fprintf(out, "%-15s failed: %v\n", app.name, err)
			if sandboxBlocked(err) {
				blocked = true
				fmt.Fprintf(out, "%-15s your sandbox blocks writing this app's settings (outside your workspace)\n", "")
			}
			continue
		}
		if did != notThere && (app.name != "chatgpt" || chatGPTDesktopFound()) { // not ChatGPT on the web's steps
			changed = append(changed, app.title)
		}
		fmt.Fprintf(out, "%-15s %s\n", app.name, did)
	}
	for _, app := range agentApps { // apps that share settings (ChatGPT Desktop reads Codex's) change together
		if app.connected() != connectedBefore[app.name] && !slices.Contains(changed, app.title) {
			changed = append(changed, app.title)
		}
	}
	apps := strings.Join(changed, " and ")
	switch {
	case len(changed) == 0:
	case remove:
		fmt.Fprintf(out, "\nRestart %s to take jm's tools out of sessions already open.\n", apps)
	case sessionAgent() == "": // a person in a terminal
		fmt.Fprintf(out, "\nRestart %s, then open your game's folder there to work on it with jm's tools.\n", apps)
	default:
		where := "the game's folder"
		if cwd, err := os.Getwd(); err == nil && exists(filepath.Join(cwd, archive.ManifestEntryKey)) {
			where = cwd
		}
		fmt.Fprintf(out, "\nSessions already open don't have jm's tools yet: restart this session in %s\n"+
			"to get them (or call open_game then). Until then, use the jm command.\n", where)
	}
	verb, again := "set up", "jm setup"
	if remove {
		verb, again = "removed", "jm setup --remove "+strings.Join(failed, " ")
	}
	switch {
	case blocked:
		return fmt.Errorf("not %s: %s. A sandbox blocked it: run `%s` again with escalated permissions "+
			"(ask the person to approve it), or ask them to run `%s` in a terminal", verb, strings.Join(failed, ", "), again, again)
	case len(failed) > 0:
		return fmt.Errorf("not %s: %s (why is above)", verb, strings.Join(failed, ", "))
	}
	return nil
}

// notThere is what a remove says when the app had no jm entry: nothing changed.
const notThere = "had no journeyman entry"

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
	return "added journeyman to " + claudeCodeConfig() + " (user scope: every project; it uses the game in the folder Claude Code runs in)", nil
}

// removeFromClaudeCode takes out the user-scope entry jm adds, through Claude Code's CLI.
func removeFromClaudeCode() (string, error) {
	// The user scope's own entry: `claude mcp get` may answer with another scope's.
	command, ok := jsonServer(claudeCodeConfig())
	elsewhere := claudeCodeElsewhere()
	if !ok && elsewhere != "" {
		return "", errors.New(elsewhere)
	}
	if !ok {
		return notThere, nil
	}
	if !isJM(command) {
		return "", fmt.Errorf("Claude Code's server called journeyman isn't jm: left alone")
	}
	if !hasCommand("claude")() {
		return "", fmt.Errorf("claude isn't on PATH here: run `claude mcp remove --scope user journeyman` where it is")
	}
	if out, err := exec.Command("claude", "mcp", "remove", "--scope", "user", "journeyman").CombinedOutput(); err != nil {
		return "", fmt.Errorf("claude mcp remove: %v: %s", err, strings.TrimSpace(string(out)))
	}
	did := "removed journeyman from " + claudeCodeConfig() + " (user scope; restart Claude Code sessions)"
	if elsewhere != "" {
		did += "; still connected: " + elsewhere
	}
	return did, nil
}

// runsJM says whether `claude mcp get` describes a server whose command is a jm.
func runsJM(description string) bool {
	for _, line := range strings.Split(description, "\n") {
		if command, ok := strings.CutPrefix(strings.TrimSpace(line), "Command:"); ok {
			return isJM(strings.TrimSpace(command))
		}
	}
	return false
}

func isJM(command string) bool {
	return strings.TrimSuffix(filepath.Base(command), ".exe") == "jm"
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
	path, err := editClaudeDesktop(func(servers map[string]any) (bool, error) {
		servers["journeyman"] = map[string]any{"command": jm, "args": []string{"mcp"}}
		return true, nil
	})
	if err != nil {
		return "", err
	}
	return "added journeyman to " + path + " (restart Claude Desktop)", nil
}

func removeFromClaudeDesktop() (string, error) {
	removed := false
	path, err := editClaudeDesktop(func(servers map[string]any) (bool, error) {
		entry, ok := servers["journeyman"].(map[string]any)
		if servers["journeyman"] == nil {
			return false, nil
		}
		if command, _ := entry["command"].(string); !ok || !isJM(command) {
			return false, fmt.Errorf("its server called journeyman isn't jm: left alone")
		}
		delete(servers, "journeyman")
		removed = true
		return true, nil
	})
	if err != nil || !removed {
		return notThere, err
	}
	return "removed journeyman from " + path + " (restart Claude Desktop)", nil
}

// editClaudeDesktop applies change to the mcpServers of Claude Desktop's
// config, keeping the rest, and writes it back when change says it changed.
func editClaudeDesktop(change func(servers map[string]any) (bool, error)) (string, error) {
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
	if changed, err := change(servers); err != nil || !changed {
		return path, err
	}
	config["mcpServers"] = servers
	data, _ = json.MarshalIndent(config, "", "  ")
	return path, writeConfig(path, append(data, '\n'))
}

func writeConfig(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return atomicfile.WriteFile(path, data, 0o644)
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
	if err := writeConfig(path, []byte(text+entry)); err != nil {
		return "", err
	}
	return "added journeyman to " + path + " (restart the Codex app; the CLI picks it up next run)", nil
}

// codexServer is the command of Codex's journeyman server, if it has one.
func codexServer() (string, bool) {
	var config struct {
		Servers map[string]any `toml:"mcp_servers"`
	}
	_, err := toml.DecodeFile(filepath.Join(codexHome(), "config.toml"), &config)
	entry, _ := config.Servers["journeyman"].(map[string]any)
	command, _ := entry["command"].(string)
	return command, err == nil && config.Servers["journeyman"] != nil
}

// removeFromCodex takes jm's table out of Codex's config.toml: through the
// codex CLI when there is one, else only where addToCodex could have written it.
func removeFromCodex() (string, error) {
	path := filepath.Join(codexHome(), "config.toml")
	command, ok := codexServer()
	if !ok {
		return notThere, nil
	}
	if !isJM(command) {
		return "", fmt.Errorf("its server called journeyman isn't jm: left alone")
	}
	done := "removed journeyman from " + path + " (restart the Codex app)"
	if hasCommand("codex")() {
		if out, err := exec.Command("codex", "mcp", "remove", "journeyman").CombinedOutput(); err != nil {
			return "", fmt.Errorf("codex mcp remove: %v: %s", err, strings.TrimSpace(string(out)))
		}
		return done, nil
	}
	old, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	tables := codexEntryPattern.FindAllStringIndex(string(old), -1)
	whole := len(tables) > 0
	for _, m := range tables {
		whole = whole && codexTableEnds(string(old[m[1]:]))
	}
	rest := strings.TrimRight(codexEntryPattern.ReplaceAllString(string(old), ""), "\n")
	if !whole || codexUnsafe.MatchString(rest) {
		return "", fmt.Errorf("%s is more than jm can safely edit: take [mcp_servers.journeyman] out by hand", path)
	}
	if rest != "" {
		rest += "\n"
	}
	return done, writeConfig(path, []byte(rest))
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

// ChatGPT Desktop reads its MCP servers from Codex's config.toml; ChatGPT on
// the web reaches them only over the internet.
func addToChatGPT(jm string) (string, error) {
	if !chatGPTDesktopFound() {
		return chatGPTWebSteps, nil
	}
	if _, err := addToCodex(jm); err != nil {
		return "", err
	}
	return "added journeyman to " + filepath.Join(codexHome(), "config.toml") + ", which ChatGPT Desktop shares with Codex (restart ChatGPT)", nil
}

func removeFromChatGPT() (string, error) {
	if !chatGPTDesktopFound() {
		return "remove it in ChatGPT: Settings > Connectors", nil
	}
	return removeFromCodex()
}

func chatGPTServer() (string, bool) {
	if !chatGPTDesktopFound() {
		return "", false
	}
	return codexServer()
}

// systemApps is where macOS installs apps for every user.
var systemApps = "/Applications"

func chatGPTDesktopFound() bool {
	switch runtime.GOOS {
	case "darwin":
		return exists(filepath.Join(systemApps, "ChatGPT.app")) || exists(filepath.Join(homeDir(), "Applications", "ChatGPT.app"))
	case "windows": // the installer's, else the Microsoft Store's
		local := os.Getenv("LOCALAPPDATA")
		store, _ := filepath.Glob(filepath.Join(local, "Packages", "OpenAI.ChatGPT-Desktop_*"))
		return local != "" && (exists(filepath.Join(local, "Programs", "ChatGPT", "ChatGPT.exe")) || len(store) > 0)
	}
	return false
}

const chatGPTWebSteps = `ChatGPT Desktop isn't here; ChatGPT on the web needs a public URL, so it's by hand:
                1. jm mcp --http 127.0.0.1:8787 --dir <your game>  (prints the URL path to use)
                2. expose it with a tunnel (e.g. cloudflared tunnel --url http://127.0.0.1:8787)
                3. ChatGPT: Settings > Connectors > Developer mode, add the tunnel URL + that path`

func homeDir() string {
	home, _ := os.UserHomeDir()
	return home
}
