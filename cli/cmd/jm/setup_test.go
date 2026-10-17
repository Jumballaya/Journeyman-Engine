package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// The machine's own apps (ChatGPT.app in /Applications) and the person's
// home and settings (their agent apps', their list of games, which a server
// outside a game opens the last of) stay out of every test.
func TestMain(m *testing.M) {
	systemApps = filepath.Join(os.TempDir(), "jm-tests-no-apps")
	os.Setenv("LOCALAPPDATA", systemApps)
	home, _ := os.MkdirTemp("", "jm-tests-home")
	if real, err := os.UserHomeDir(); err == nil && os.Getenv("JM_TOOLCHAIN_DIR") == "" {
		os.Setenv("JM_TOOLCHAIN_DIR", filepath.Join(real, ".jm", "toolchains")) // the toolchain jm uses, kept
	}
	for _, k := range []string{"HOME", "USERPROFILE", "APPDATA", "XDG_DATA_HOME"} {
		os.Setenv(k, home)
	}
	code := m.Run()
	os.RemoveAll(home)
	os.Exit(code)
}

// ChatGPT Desktop shares Codex's config.toml; without it, ChatGPT is the web's, set up by hand.
func TestChatGPTDesktopIsSetUpThroughCodexsConfig(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("finds ChatGPT.app")
	}
	home := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("CODEX_HOME", t.TempDir())
	t.Setenv("PATH", t.TempDir()) // no codex CLI
	t.Setenv("CLAUDECODE", "")
	t.Setenv("CODEX_THREAD_ID", "")
	var out bytes.Buffer
	if err := setupAgents([]string{"chatgpt"}, "/x/jm", false, &out); err != nil || !strings.Contains(out.String(), "tunnel") {
		t.Fatalf("without ChatGPT Desktop: %v\n%s", err, out.String())
	}
	if exists(filepath.Join(codexHome(), "config.toml")) || !strings.Contains(statuses(), "chatgpt manual") {
		t.Errorf("web steps only, but %s", statuses())
	}

	os.MkdirAll(filepath.Join(home, "Applications", "ChatGPT.app"), 0o755)
	if got := statuses(); !strings.Contains(got, "codex not-installed, chatgpt not-connected") {
		t.Errorf("ChatGPT Desktop found: %s", got)
	}
	out.Reset()
	if err := setupAgents([]string{"chatgpt"}, "/x/jm", false, &out); err != nil || !strings.Contains(out.String(), "shares it with Codex") || !strings.Contains(out.String(), "Restart ChatGPT") {
		t.Fatalf("setup chatgpt: %v\n%s", err, out.String())
	}
	if command, ok := codexServer(); !ok || command != "/x/jm" || !strings.Contains(statuses(), "codex connected, chatgpt connected") {
		t.Errorf("after setup: %s (%q)", statuses(), command)
	}
	out.Reset()
	if err := setupAgents([]string{"codex"}, "", true, &out); err != nil || !strings.Contains(out.String(), "Restart Codex and ChatGPT") {
		t.Fatalf("remove through Codex: %v\n%s", err, out.String())
	}
	if _, ok := codexServer(); ok {
		t.Error("still in Codex's config after setup --remove codex")
	}
}

func TestCodexSetupReplacesOnlyItsOwnTable(t *testing.T) {
	t.Setenv("PATH", t.TempDir()) // no codex CLI: jm edits the file itself
	t.Setenv("CODEX_HOME", t.TempDir())
	path := filepath.Join(codexHome(), "config.toml")
	before := "model = \"x\"\npath = \"C:\\\\Users\\\\me\"\n\n[mcp_servers.journeyman]\ncommand = \"/old/jm\"\nargs = [\"mcp\"]\n\n[mcp_servers.\"other#name\"]\ncommand = \"q\"\n\n[mcp_servers.other]\ncommand = \"o\"\nmatrix = [\n[1, 2],\n]\n"
	os.WriteFile(path, []byte(before), 0o644)
	for range 2 { // twice: no duplicate table
		if _, err := addToCodex(`C:\jm\jm.exe`); err != nil {
			t.Fatal(err)
		}
	}
	got, _ := os.ReadFile(path)
	text := string(got)
	if strings.Count(text, "[mcp_servers.journeyman]") != 1 || strings.Contains(text, "/old/jm") {
		t.Errorf("the old entry survived or doubled:\n%s", text)
	}
	if !strings.Contains(text, `command = "C:\\jm\\jm.exe"`) || !strings.Contains(text, "[mcp_servers.other]") || !strings.Contains(text, `model = "x"`) || !strings.Contains(text, "[1, 2],") || !strings.Contains(text, `"other#name"`) {
		t.Errorf("lost other settings or mis-quoted the path:\n%s", text)
	}
}

func TestClaudeDesktopSetupKeepsOtherServers(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	t.Setenv("APPDATA", t.TempDir())
	path := claudeDesktopConfig()
	os.MkdirAll(filepath.Dir(path), 0o755)
	os.WriteFile(path, []byte(`{"mcpServers": {"other": {"command": "o"}}, "theme": "dark"}`), 0o644)
	if _, err := addToClaudeDesktop("/x/jm"); err != nil {
		t.Fatal(err)
	}
	var config struct {
		Servers map[string]struct {
			Command string   `json:"command"`
			Args    []string `json:"args"`
		} `json:"mcpServers"`
		Theme string `json:"theme"`
	}
	data, _ := os.ReadFile(path)
	if err := json.Unmarshal(data, &config); err != nil {
		t.Fatal(err)
	}
	if j := config.Servers["journeyman"]; j.Command != "/x/jm" || len(j.Args) != 1 || j.Args[0] != "mcp" {
		t.Errorf("journeyman entry: %+v", j)
	}
	if config.Servers["other"].Command != "o" || config.Theme != "dark" {
		t.Errorf("lost the app's own settings: %s", data)
	}

	os.WriteFile(path, []byte(`{ not json`), 0o644)
	if _, err := addToClaudeDesktop("/x/jm"); err == nil {
		t.Error("a broken config must be left alone, not overwritten")
	}
}

func TestSetupWithoutAgentsFindsNoneAndSaysSo(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	t.Setenv("APPDATA", t.TempDir())
	t.Setenv("CODEX_HOME", filepath.Join(t.TempDir(), "absent"))
	t.Setenv("PATH", t.TempDir())
	t.Setenv("CLAUDECODE", "") // and not run from an agent app
	t.Setenv("CODEX_THREAD_ID", "")
	var out bytes.Buffer
	if err := setupAgents(nil, "/x/jm", false, &out); err == nil || !strings.Contains(err.Error(), "found no agent apps") {
		t.Errorf("err = %v", err)
	}
	if err := setupAgents([]string{"cursor"}, "/x/jm", false, &out); err == nil {
		t.Error("an unknown agent must be an error")
	}
}

// Plain setup includes the app this session runs in, as doctor lists it, even when it's found nowhere else.
func TestSetupIncludesTheSessionsApp(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	t.Setenv("APPDATA", t.TempDir())
	t.Setenv("CODEX_HOME", t.TempDir()) // no config.toml yet
	t.Setenv("PATH", t.TempDir())
	t.Setenv("CLAUDECODE", "")
	t.Setenv("CODEX_THREAD_ID", "t")
	var out bytes.Buffer
	if err := setupAgents(nil, "/x/jm", false, &out); err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if got := statuses(); got != "claude-code not-installed, claude-desktop not-installed, codex connected (here), chatgpt manual" {
		t.Errorf("after setup: %v", got)
	}
	// Claude Code's CLI is how it's set up: without it, setup says what to run.
	t.Setenv("CODEX_THREAD_ID", "")
	t.Setenv("CLAUDECODE", "1")
	out.Reset()
	if err := setupAgents(nil, "/x/jm", false, &out); err == nil || !strings.Contains(out.String(), "claude mcp add --scope user journeyman -- '/x/jm' mcp") {
		t.Errorf("%v\n%s", err, out.String())
	}
}

// statuses is agentStates in brief: "codex connected (here), ...".
func statuses() string {
	var out []string
	for _, a := range agentStates() {
		s := a.Name + " " + a.Status
		if a.ThisSession {
			s += " (here)"
		}
		out = append(out, s)
	}
	return strings.Join(out, ", ")
}

// fakeCLI puts a shell-script name on PATH that logs its arguments and answers
// "get" with getOut (exit 1 when it's empty: no such server).
func fakeCLI(t *testing.T, name, getOut string) (log string) {
	t.Helper()
	if runtime.GOOS == "windows" {
		t.Skip("a shell-script " + name)
	}
	bin := t.TempDir()
	log = filepath.Join(bin, "calls")
	get := "exit 1"
	if getOut != "" {
		get = "echo '" + getOut + "'; exit 0"
	}
	script := "#!/bin/sh\nif [ \"$2\" = get ]; then " + get + "; fi\necho \"$@\" >> " + log + "\n"
	os.WriteFile(filepath.Join(bin, name), []byte(script), 0o755)
	t.Setenv("PATH", bin)
	return log
}

func TestSetupClaudeCodeGoesThroughItsCLI(t *testing.T) {
	log := fakeCLI(t, "claude", "")
	if _, err := addToClaudeCode("/x/jm"); err != nil {
		t.Fatal(err)
	}
	calls, _ := os.ReadFile(log)
	if !strings.Contains(string(calls), "mcp add --scope user journeyman -- /x/jm mcp") || strings.Contains(string(calls), "remove") {
		t.Errorf("calls:\n%s", calls)
	}

	log = fakeCLI(t, "claude", "journeyman:\n  Type: stdio\n  Command: /old/bin/jm\n  Args: mcp")
	if _, err := addToClaudeCode("/x/jm"); err != nil {
		t.Fatal(err)
	}
	if calls, _ := os.ReadFile(log); !strings.Contains(string(calls), "mcp remove --scope user journeyman") {
		t.Errorf("an older jm entry must be replaced:\n%s", calls)
	}

	log = fakeCLI(t, "claude", "journeyman:\n  Type: http\n  URL: https://jm.example.com/mcp")
	if _, err := addToClaudeCode("/x/jm"); err == nil {
		t.Error("someone else's journeyman server must be left alone")
	}
	if calls, _ := os.ReadFile(log); len(calls) > 0 {
		t.Errorf("touched it anyway:\n%s", calls)
	}
}

func TestCodexSetupUsesItsCLIWhenThere(t *testing.T) {
	t.Setenv("CODEX_HOME", t.TempDir())
	log := fakeCLI(t, "codex", "")
	if _, err := addToCodex("/x/jm"); err != nil {
		t.Fatal(err)
	}
	if calls, _ := os.ReadFile(log); !strings.Contains(string(calls), "mcp add journeyman -- /x/jm mcp") {
		t.Errorf("calls:\n%s", calls)
	}
}

func TestCodexSetupRefusesWhatItCantSafelyEdit(t *testing.T) {
	t.Setenv("PATH", t.TempDir())
	for _, doc := range []string{
		"[mcp_servers.\"journeyman\"]\ncommand = \"/old\"\n",
		"mcp_servers = { journeyman = { command = \"/old\" } }\n",
		"notes = \"\"\"\n[mcp_servers.journeyman]\n\"\"\"\n",
		"mcp_servers = {}\n",
		"[mcp_servers.\"journey\\u006dan\"]\ncommand = \"/old\"\n",
		"[mcp_servers.journeyman]\ncommand = \"/old/jm\"\nargs = [\"mcp\"]\n\n[mcp_servers.journeyman.env]\nA = \"1\"\n",
		"[mcp_servers.other]\ncommand = \"o\"\n\n[mcp_servers.journeyman]\ncommand = \"/old/jm\"\nargs = [\"mcp\"]\nenv = { TOKEN = \"t\" }\nenabled = false\n",
		"mcp_servers.journeyman.command = \"/old/jm\"\nmcp_servers.journeyman.args = [\"mcp\"]\n",
		"[mcp_servers]\n\"journeyman\".command = \"/old/jm\"\n",
		"[mcp_servers]\n\"journey\\u006dan\".command = \"/old/jm\"\n",
	} {
		t.Setenv("CODEX_HOME", t.TempDir())
		path := filepath.Join(codexHome(), "config.toml")
		os.WriteFile(path, []byte(doc), 0o644)
		if _, err := addToCodex("/x/jm"); err == nil {
			t.Errorf("edited a config it can't parse safely:\n%s", doc)
		}
		if got, _ := os.ReadFile(path); string(got) != doc {
			t.Errorf("changed it anyway:\n%s", got)
		}
	}
}

func TestClaudeDesktopSetupRejectsOddShapes(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	t.Setenv("APPDATA", t.TempDir())
	path := claudeDesktopConfig()
	os.MkdirAll(filepath.Dir(path), 0o755)
	for _, doc := range []string{`null`, `{"mcpServers": ["x"]}`} {
		os.WriteFile(path, []byte(doc), 0o644)
		if _, err := addToClaudeDesktop("/x/jm"); err == nil {
			t.Errorf("accepted %s", doc)
		}
	}
}

func TestDoctorNamesAgentAppsNotConnectedYet(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	t.Setenv("APPDATA", t.TempDir())
	t.Setenv("CODEX_HOME", t.TempDir())
	os.WriteFile(filepath.Join(codexHome(), "config.toml"), []byte("model = \"x\"\n"), 0o644) // Codex is here
	t.Setenv("PATH", t.TempDir())
	t.Setenv("CODEX_THREAD_ID", "")
	t.Setenv("CLAUDECODE", "1") // run from Claude Code, whose claude isn't on PATH
	if got := statuses(); got != "claude-code not-connected (here), claude-desktop not-installed, codex not-connected, chatgpt manual" {
		t.Fatalf("before setup: %v", got)
	}
	if _, err := addToCodex("/x/jm"); err != nil {
		t.Fatal(err)
	}
	if got := statuses(); got != "claude-code not-connected (here), claude-desktop not-installed, codex connected, chatgpt manual" {
		t.Errorf("after setup: %v", got)
	}
}

// A sandbox that won't let setup write says so, and what to do; a setup that
// worked says the open session needs a restart.
func TestSetupSaysWhenASandboxBlocksIt(t *testing.T) {
	if runtime.GOOS == "windows" || os.Getuid() == 0 {
		t.Skip("needs a folder this user can't write")
	}
	t.Setenv("PATH", t.TempDir())
	t.Setenv("CODEX_HOME", t.TempDir())
	os.Chmod(codexHome(), 0o555)
	defer os.Chmod(codexHome(), 0o755)
	var out bytes.Buffer
	err := setupAgents([]string{"codex"}, "/x/jm", false, &out)
	if err == nil || !strings.Contains(err.Error(), "escalated permissions") || !strings.Contains(out.String(), "your sandbox blocks") {
		t.Errorf("err %v, out:\n%s", err, out.String())
	}
	if strings.Contains(out.String(), "restart") {
		t.Errorf("nothing was set up, yet:\n%s", out.String())
	}

	os.Chmod(codexHome(), 0o755)
	t.Setenv("CLAUDECODE", "")
	t.Setenv("CODEX_THREAD_ID", "t") // an agent's session: restart it
	out.Reset()
	if err := setupAgents([]string{"codex"}, "/x/jm", false, &out); err != nil || !strings.Contains(out.String(), "restart this session") {
		t.Errorf("err %v, out:\n%s", err, out.String())
	}
	t.Setenv("CODEX_THREAD_ID", "") // a person in a terminal: restart the app, open the game there
	out.Reset()
	if err := setupAgents([]string{"codex"}, "/x/jm", false, &out); err != nil || !strings.Contains(out.String(), "Restart Codex, then open your game's folder") {
		t.Errorf("err %v, out:\n%s", err, out.String())
	}
}

func TestConnectedChecksReadEveryScopeAndSpelling(t *testing.T) {
	home := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("CODEX_HOME", t.TempDir())
	wd, _ := os.Getwd()
	defer os.Chdir(wd)
	project := t.TempDir()
	os.Chdir(project)
	cwd, _ := os.Getwd()
	local, _ := json.Marshal(map[string]any{"projects": map[string]any{cwd: map[string]any{"mcpServers": map[string]any{"journeyman": map[string]any{}}}}})
	os.WriteFile(filepath.Join(home, ".claude.json"), local, 0o644)
	if !strings.Contains(claudeCodeElsewhere(), "--scope local") {
		t.Error("a project-scoped journeyman is connected")
	}
	os.WriteFile(filepath.Join(home, ".claude.json"), []byte(`{}`), 0o644)
	os.WriteFile(".mcp.json", []byte(`{"mcpServers": {"journeyman": {}}}`), 0o644)
	if !strings.Contains(claudeCodeElsewhere(), ".mcp.json") {
		t.Error("a .mcp.json journeyman is connected")
	}

	codex, _ := findAgentApp("codex")
	path := filepath.Join(codexHome(), "config.toml")
	for doc, want := range map[string]bool{
		"[mcp_servers.\"journeyman\"]\ncommand = \"x\"\n":      true,
		"[mcp_servers.'journeyman']\ncommand = \"x\"\n":        true,
		"[mcp_servers]\njourneyman.command = \"x\"\n":          true,
		"[mcp_servers.journeyman]\nenv = {\n  A = \"1\",\n}\n": true, // TOML 1.1, as Codex reads it
		"# [mcp_servers.journeyman]\n":                         false,
		"note = \"\"\"\n[mcp_servers.journeyman]\n\"\"\"\n":    false,
	} {
		os.WriteFile(path, []byte(doc), 0o644)
		if _, ok := codex.server(); ok != want {
			t.Errorf("%q: connected = %v", doc, !want)
		}
	}
	os.Remove(path)
	t.Setenv("PATH", t.TempDir())
	if codex.found() {
		t.Error("an empty CODEX_HOME isn't Codex")
	}
}

// Removing undoes setup and nothing else: the app's other settings stay, and
// a journeyman that isn't a jm is left alone.
func TestRemoveTakesOutOnlyJMsEntry(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	t.Setenv("APPDATA", t.TempDir())
	t.Setenv("CODEX_HOME", t.TempDir())
	t.Setenv("PATH", t.TempDir()) // no codex CLI: jm edits the file itself
	desktop := claudeDesktopConfig()
	os.MkdirAll(filepath.Dir(desktop), 0o755)
	os.WriteFile(desktop, []byte(`{"mcpServers": {"other": {"command": "o"}}}`), 0o644)
	codex := filepath.Join(codexHome(), "config.toml")
	before := "model = \"x\"\n\n[mcp_servers.other]\ncommand = \"o\"\n"
	os.WriteFile(codex, []byte(before), 0o644)
	addToClaudeDesktop("/x/jm")
	addToCodex("/x/jm")

	var out bytes.Buffer
	if err := setupAgents(nil, "", true, &out); err != nil {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if !strings.Contains(out.String(), "removed journeyman from "+codex) || !strings.Contains(out.String(), "Restart Claude Desktop and Codex to take") {
		t.Errorf("doesn't say what changed:\n%s", out.String())
	}
	if got, _ := os.ReadFile(codex); string(got) != before {
		t.Errorf("codex config isn't as it was:\n%s", got)
	}
	if data, _ := os.ReadFile(desktop); hasServer(desktop) || !strings.Contains(string(data), `"other"`) {
		t.Errorf("claude desktop config:\n%s", data)
	}
	out.Reset()
	if err := setupAgents(nil, "", true, &out); err != nil || !strings.Contains(out.String(), "nothing to remove") {
		t.Errorf("a second remove: %v\n%s", err, out.String())
	}

	someoneElses := "[mcp_servers.journeyman]\ncommand = \"/opt/other-server\"\n"
	os.WriteFile(codex, []byte(someoneElses), 0o644)
	os.WriteFile(desktop, []byte(`{"mcpServers": {"journeyman": {"url": "https://x"}}}`), 0o644)
	for _, name := range []string{"codex", "claude-desktop"} {
		if err := setupAgents([]string{name}, "", true, &out); err == nil {
			t.Errorf("%s: removed someone else's journeyman", name)
		}
	}
	if got, _ := os.ReadFile(codex); string(got) != someoneElses || !hasServer(desktop) {
		t.Errorf("changed it anyway:\n%s", got)
	}
}

func TestRemoveGoesThroughTheAppsCLI(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	log := fakeCLI(t, "claude", "")
	os.WriteFile(claudeCodeConfig(), []byte(`{"mcpServers": {"journeyman": {"command": "/opt/other"}}}`), 0o644)
	if _, err := removeFromClaudeCode(); err == nil {
		t.Error("removed a user-scope journeyman that isn't jm")
	}
	os.WriteFile(claudeCodeConfig(), []byte(`{"mcpServers": {"journeyman": {"command": "/old/bin/jm"}}}`), 0o644)
	if _, err := removeFromClaudeCode(); err != nil {
		t.Fatal(err)
	}
	if calls, _ := os.ReadFile(log); !strings.Contains(string(calls), "mcp remove --scope user journeyman") {
		t.Errorf("calls:\n%s", calls)
	}
	log = fakeCLI(t, "claude", "")
	os.WriteFile(claudeCodeConfig(), []byte(`{"projects": {"/p": {"mcpServers": {"journeyman": {"command": "jm"}}}}}`), 0o644)
	if did, err := removeFromClaudeCode(); err != nil || did != notThere {
		t.Errorf("nothing to remove: %q, %v", did, err)
	}
	if calls, _ := os.ReadFile(log); len(calls) > 0 {
		t.Errorf("removed something anyway:\n%s", calls)
	}

	t.Setenv("CODEX_HOME", t.TempDir())
	os.WriteFile(filepath.Join(codexHome(), "config.toml"), []byte(codexEntry("/x/jm")), 0o644)
	log = fakeCLI(t, "codex", "")
	if _, err := removeFromCodex(); err != nil {
		t.Fatal(err)
	}
	if calls, _ := os.ReadFile(log); !strings.Contains(string(calls), "mcp remove journeyman") {
		t.Errorf("calls:\n%s", calls)
	}
}

// Claude Code's local and project scopes count as connected, but jm setup
// --remove only takes out the user scope's entry it added: for the others it
// fails with the steps, changing nothing.
func TestRemoveSaysHowToRemoveClaudeCodeScopesItDidntWrite(t *testing.T) {
	home := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("CODEX_HOME", t.TempDir())
	t.Setenv("CLAUDECODE", "")
	t.Setenv("CODEX_THREAD_ID", "")
	wd, _ := os.Getwd()
	defer os.Chdir(wd)
	os.Chdir(t.TempDir())
	cwd, _ := os.Getwd()
	user := map[string]any{"journeyman": map[string]any{"command": "/x/jm"}}
	local := map[string]any{cwd: map[string]any{"mcpServers": map[string]any{"journeyman": map[string]any{}}}}
	claudeCode := func() agentState { return agentStates()[0] }
	for _, c := range []struct {
		name               string
		user, local, share bool
	}{{"user", true, false, false}, {"local", false, true, false}, {"project", false, false, true}, {"mixed", true, false, true}} {
		log := fakeCLI(t, "claude", "")
		config := map[string]any{}
		if c.user {
			config["mcpServers"] = user
		}
		if c.local {
			config["projects"] = local
		}
		data, _ := json.Marshal(config)
		os.WriteFile(claudeCodeConfig(), data, 0o644)
		os.Remove(".mcp.json")
		if c.share {
			os.WriteFile(".mcp.json", []byte(`{"mcpServers": {"journeyman": {}}}`), 0o644)
		}
		if s := claudeCode(); s.Status != "connected" || s.Removable != c.user || (s.Elsewhere != "") != (c.local || c.share) {
			t.Errorf("%s: %+v", c.name, s)
		}
		var out bytes.Buffer
		err := setupAgents([]string{"claude-code"}, "", true, &out)
		calls, _ := os.ReadFile(log)
		removed := strings.Contains(string(calls), "mcp remove --scope user journeyman")
		switch {
		case c.user && (err != nil || !removed):
			t.Errorf("%s: %v, %v\n%s", c.name, err, removed, out.String())
		case !c.user && (err == nil || removed || !strings.Contains(out.String(), "didn't add")):
			t.Errorf("%s: a remove that can't remove must fail with the steps: %v\n%s", c.name, err, out.String())
		case c.share && c.user && !strings.Contains(out.String(), "still connected: "):
			t.Errorf("%s: doesn't say what's left:\n%s", c.name, out.String())
		}
	}
	// Removing from every app includes one connected only elsewhere: it says how.
	os.WriteFile(claudeCodeConfig(), []byte(`{}`), 0o644)
	out := bytes.Buffer{}
	if err := setupAgents(nil, "", true, &out); err == nil || !strings.Contains(out.String(), ".mcp.json") {
		t.Errorf("remove all: %v\n%s", err, out.String())
	}
	t.Setenv("PATH", t.TempDir()) // without claude, the steps are still the right ones
	if _, err := removeFromClaudeCode(); err == nil || !strings.Contains(err.Error(), ".mcp.json") {
		t.Errorf("no claude: %v", err)
	}
	// A journeyman that isn't jm is connected, but not jm's to remove.
	os.Remove(".mcp.json")
	os.WriteFile(claudeCodeConfig(), []byte(`{"mcpServers": {"journeyman": {"command": "/opt/other"}}}`), 0o644)
	if s := claudeCode(); s.Status != "connected" || s.Removable {
		t.Errorf("someone else's: %+v", s)
	}
}

// An agent whose own app lacks jm hears how to connect where it starts out (init, build, run), until it's connected.
func TestAnUnconnectedAgentIsToldToConnect(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	t.Setenv("CODEX_HOME", t.TempDir())
	t.Setenv("PATH", t.TempDir())
	t.Setenv("CLAUDECODE", "")
	t.Setenv("CODEX_THREAD_ID", "t")
	initGame := func() string {
		t.Chdir(t.TempDir())
		var stdout, stderr bytes.Buffer
		root := newRootCmd()
		root.SetArgs([]string{"init", "game"})
		root.SetOut(&stdout)
		root.SetErr(&stderr)
		if err := root.Execute(); err != nil {
			t.Fatal(err)
		}
		return stderr.String()
	}
	if got := initGame(); !strings.Contains(got, "codex isn't connected to jm") || !strings.Contains(got, "run `jm setup`") {
		t.Errorf("unconnected, init said: %q", got)
	}
	var report bytes.Buffer
	doctorReport{Agents: []agentState{{Name: "codex", Status: "not-connected", ThisSession: true}}}.print(&report)
	if first, _, _ := strings.Cut(report.String(), "\n"); !strings.Contains(first, "jm setup") {
		t.Errorf("doctor's first line: %q", first)
	}

	if err := setupAgents([]string{"codex"}, "/x/jm", false, &bytes.Buffer{}); err != nil {
		t.Fatal(err)
	}
	if got := initGame(); got != "" {
		t.Errorf("connected, init still said: %q", got)
	}
	t.Setenv("CODEX_THREAD_ID", "") // and a person in a terminal never hears it
	t.Setenv("CODEX_HOME", t.TempDir())
	if got := initGame(); got != "" {
		t.Errorf("outside an agent, init said: %q", got)
	}
}

// A session whose app has jm only in a scope setup didn't write has the tools: no nudge.
func TestASessionConnectedElsewhereIsntNudged(t *testing.T) {
	t.Setenv("HOME", t.TempDir())
	t.Setenv("CODEX_THREAD_ID", "")
	t.Setenv("CLAUDECODE", "1")
	t.Chdir(t.TempDir())
	if unconnectedSession() != "claude-code" {
		t.Fatal("not connected anywhere: nudged")
	}
	os.WriteFile(".mcp.json", []byte(`{"mcpServers": {"journeyman": {}}}`), 0o644)
	if got := unconnectedSession(); got != "" {
		t.Errorf("connected through .mcp.json, nudged for %q", got)
	}
}
