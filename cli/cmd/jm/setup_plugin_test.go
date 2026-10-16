package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// pluginHome is a scratch home with Codex's settings in it, and no codex CLI.
func pluginHome(t *testing.T) string {
	home := t.TempDir()
	t.Setenv("HOME", home)
	t.Setenv("USERPROFILE", home)
	t.Setenv("CODEX_HOME", filepath.Join(home, ".codex"))
	t.Setenv("PATH", t.TempDir())
	os.MkdirAll(filepath.Join(home, ".codex"), 0o755)
	return home
}

// Setting up Codex installs the plugin as Codex would: listed in the person's
// marketplace (theirs kept), its installed copy, enabled in config.toml; removing takes it all out.
func TestCodexSetupInstallsThePlugin(t *testing.T) {
	home := pluginHome(t)
	market := filepath.Join(home, ".agents", "plugins", "marketplace.json")
	os.MkdirAll(filepath.Dir(market), 0o755)
	mine := `{"name": "mine", "plugins": [{"name": "other", "source": {"source": "local", "path": "./x"}}]}`
	os.WriteFile(market, []byte(mine), 0o644)
	config := filepath.Join(codexHome(), "config.toml")
	before := "model = \"x\"\n"
	os.WriteFile(config, []byte(before), 0o644)
	for range 2 {
		if did, err := addToCodex("/x/jm"); err != nil || !strings.Contains(did, "its plugin") {
			t.Fatalf("%s %v", did, err)
		}
	}
	var listed struct {
		Name    string           `json:"name"`
		Plugins []map[string]any `json:"plugins"`
	}
	data, _ := os.ReadFile(market)
	json.Unmarshal(data, &listed)
	if listed.Name != "mine" || len(listed.Plugins) != 2 || jsonOf(listed.Plugins[1]["source"]) != `{"path":"./.codex/plugins/journeyman","source":"local"}` {
		t.Errorf("marketplace: %s", data)
	}
	for _, f := range []string{"plugins/journeyman/plugin.json", "plugins/cache/mine/journeyman/local/mcp.json"} {
		if !exists(filepath.Join(codexHome(), f)) {
			t.Errorf("no %s", f)
		}
	}
	got, _ := os.ReadFile(config)
	if strings.Count(string(got), `[plugins."journeyman@mine"]`) != 1 || !strings.Contains(string(got), "[mcp_servers.journeyman]") {
		t.Errorf("config.toml:\n%s", got)
	}

	var out bytes.Buffer
	if err := setupAgents([]string{"codex"}, "", true, &out); err != nil || !strings.Contains(out.String(), "and its plugin") {
		t.Fatalf("%v\n%s", err, out.String())
	}
	if got, _ := os.ReadFile(config); string(got) != before {
		t.Errorf("config.toml after remove:\n%s", got)
	}
	data, _ = os.ReadFile(market)
	if strings.Contains(string(data), "journeyman") || !strings.Contains(string(data), `"other"`) || exists(filepath.Join(codexHome(), "plugins", "journeyman")) || exists(filepath.Join(codexHome(), "plugins", "cache", "mine", "journeyman")) {
		t.Errorf("left behind:\n%s", data)
	}
}

// With the codex CLI, it installs the plugin (and keeps Codex's own copy out of jm's hands).
func TestCodexSetupInstallsThePluginThroughItsCLI(t *testing.T) {
	pluginHome(t)
	log := fakeCLI(t, "codex", "")
	if _, err := addToCodex("/x/jm"); err != nil {
		t.Fatal(err)
	}
	calls, _ := os.ReadFile(log)
	if !strings.Contains(string(calls), "plugin add journeyman@codex-curated") || exists(filepath.Join(codexHome(), "plugins", "cache")) {
		t.Errorf("calls:\n%s", calls)
	}
}

// A new session in a game's folder (or below it) is told which game it is; elsewhere, nothing.
func TestSessionStartNamesTheGame(t *testing.T) {
	game := t.TempDir()
	os.WriteFile(filepath.Join(game, ".jm.json"), []byte(`{"name": "Neon Vow"}`), 0o644)
	os.MkdirAll(filepath.Join(game, "scenes"), 0o755)
	var out bytes.Buffer
	if err := sessionStart(strings.NewReader(`{"cwd": `+jsonOf(filepath.Join(game, "scenes"))+`, "source": "startup"}`), &out); err != nil {
		t.Fatal(err)
	}
	var raw map[string]map[string]string
	json.Unmarshal(out.Bytes(), &raw)
	if raw["hookSpecificOutput"]["hookEventName"] != "SessionStart" || !strings.Contains(raw["hookSpecificOutput"]["additionalContext"], "Neon Vow") ||
		!strings.Contains(raw["hookSpecificOutput"]["additionalContext"], "open_game") {
		t.Errorf("said: %s", out.String())
	}
	out.Reset()
	sessionStart(strings.NewReader(`{"cwd": `+jsonOf(t.TempDir())+`}`), &out)
	if out.Len() != 0 {
		t.Errorf("outside a game: %s", out.String())
	}
}

// A new game brings the plugin in its own marketplace, offered (not installed) where Codex opens it.
func TestInitOffersThePluginInTheGame(t *testing.T) {
	dir := t.TempDir()
	if err := runInit(dir, "Neon Vow", &bytes.Buffer{}); err != nil {
		t.Fatal(err)
	}
	data, _ := os.ReadFile(filepath.Join(dir, ".agents", "plugins", "marketplace.json"))
	var m struct {
		Plugins []map[string]any `json:"plugins"`
	}
	json.Unmarshal(data, &m)
	if len(m.Plugins) != 1 || jsonOf(m.Plugins[0]["source"]) != `{"path":"./plugins/journeyman","source":"local"}` ||
		m.Plugins[0]["policy"].(map[string]any)["installation"] != "AVAILABLE" {
		t.Errorf("marketplace: %s", data)
	}
	server, _ := os.ReadFile(filepath.Join(dir, "plugins", "journeyman", "mcp.json"))
	if !strings.Contains(string(server), `"command": "jm"`) {
		t.Errorf("the game's plugin runs the jm on PATH: %s", server)
	}
}

// A config or marketplace jm can't safely edit is left as it is, the person told how.
func TestPluginSetupRefusesWhatItCantSafelyEdit(t *testing.T) {
	for _, doc := range []string{"plugins = {}\n", "[plugins]\n\"journeyman@codex-curated\".enabled = false\n", "plugins.\"journeyman@x\".enabled = false\n"} {
		pluginHome(t)
		path := filepath.Join(codexHome(), "config.toml")
		os.WriteFile(path, []byte(doc), 0o644)
		if _, err := addToCodex("/x/jm"); err == nil {
			t.Errorf("edited:\n%s", doc)
		}
		if got, _ := os.ReadFile(path); string(got) != doc {
			t.Errorf("changed it anyway:\n%s", got)
		}
	}
	home := pluginHome(t)
	market := filepath.Join(home, ".agents", "plugins", "marketplace.json")
	os.MkdirAll(filepath.Dir(market), 0o755)
	os.WriteFile(market, []byte(`{"name": "../../../Documents", "plugins": []}`), 0o644)
	if did, _ := addToCodex("/x/jm"); !strings.Contains(did, "its plugin wasn't") {
		t.Errorf("a marketplace name that's a path: %s", did)
	}
	if _, err := uninstallPlugin(); err == nil {
		t.Error("uninstalled through a path name")
	}
}
