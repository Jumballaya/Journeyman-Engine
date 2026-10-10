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
	var out bytes.Buffer
	if err := setupAgents(nil, "/x/jm", &out); err == nil || !strings.Contains(err.Error(), "found no agent apps") {
		t.Errorf("err = %v", err)
	}
	if err := setupAgents([]string{"cursor"}, "/x/jm", &out); err == nil {
		t.Error("an unknown agent must be an error")
	}
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
	if got := unconnectedAgents(); strings.Join(got, ",") != "codex" {
		t.Fatalf("before setup: %v", got)
	}
	if _, err := addToCodex("/x/jm"); err != nil {
		t.Fatal(err)
	}
	if got := unconnectedAgents(); len(got) != 0 {
		t.Errorf("after setup: %v", got)
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
	if !claudeCodeConnected() {
		t.Error("a project-scoped journeyman is connected")
	}
	os.WriteFile(filepath.Join(home, ".claude.json"), []byte(`{}`), 0o644)
	os.WriteFile(".mcp.json", []byte(`{"mcpServers": {"journeyman": {}}}`), 0o644)
	if !claudeCodeConnected() {
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
		if codex.connected() != want {
			t.Errorf("%q: connected = %v", doc, !want)
		}
	}
	os.Remove(path)
	t.Setenv("PATH", t.TempDir())
	if codex.found() {
		t.Error("an empty CODEX_HOME isn't Codex")
	}
}
