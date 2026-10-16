package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
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
	if _, err := removeFromCodex(); err == nil {
		t.Error("uninstalled through a path name")
	}
}

// tree is every file under dir and what it holds (a folder: "/").
func tree(t *testing.T, dir string) map[string]string {
	t.Helper()
	out := map[string]string{}
	filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		rel, _ := filepath.Rel(dir, path)
		if d.IsDir() {
			out[rel] = "/"
		} else {
			data, _ := os.ReadFile(path)
			out[rel] = string(data)
		}
		return nil
	})
	return out
}

// seedPlugin puts a plugin, its installed copy and a marketplace (named
// marketplace) in home, as some earlier setup left them.
func seedPlugin(t *testing.T, home, marketplace string) {
	for path, text := range map[string]string{
		".codex/plugins/journeyman/sentinel":                                 "source",
		".codex/plugins/cache/" + marketplace + "/journeyman/local/sentinel": "cache",
		".agents/plugins/marketplace.json":                                   `{"name": "` + marketplace + `", "plugins": [], "serial": 9007199254740993}`,
	} {
		path = filepath.Join(home, filepath.FromSlash(path))
		os.MkdirAll(filepath.Dir(path), 0o755)
		os.WriteFile(path, []byte(text), 0o644)
	}
}

// A config.toml jm won't edit stops setup before the plugin, the marketplace or
// Codex's copy change, for Codex and ChatGPT alike.
func TestARefusedConfigChangesNothing(t *testing.T) {
	for _, doc := range []string{"plugins = {}\n", "plugins.\"journeyman@x\".enabled = false\n", "[plugins.\"journeyman@mine\"]\nenabled = false\n"} {
		home := pluginHome(t)
		seedPlugin(t, home, "mine")
		os.WriteFile(filepath.Join(codexHome(), "config.toml"), []byte(doc), 0o644)
		os.MkdirAll(filepath.Join(home, "Applications", "ChatGPT.app"), 0o755)
		before := tree(t, home)
		setups := []func(string) (string, error){addToCodex}
		if chatGPTDesktopFound() {
			setups = append(setups, addToChatGPT)
		}
		for _, setup := range setups {
			if _, err := setup("/x/jm"); err == nil {
				t.Errorf("set up over:\n%s", doc)
			}
			if after := tree(t, home); !reflect.DeepEqual(after, before) {
				t.Errorf("changed with %q:\n%v\nwas\n%v", doc, after, before)
			}
		}
	}
}

// failingCodex is a codex CLI on PATH that fails on "<verb> <action>" (e.g. "mcp add"), else does nothing.
func failingCodex(t *testing.T, verb, action string) {
	if runtime.GOOS == "windows" {
		t.Skip("a shell-script codex")
	}
	bin := t.TempDir()
	script := "#!/bin/sh\nif [ \"$1 $2\" = \"" + verb + " " + action + "\" ]; then echo nope; exit 1; fi\n"
	os.WriteFile(filepath.Join(bin, "codex"), []byte(script), 0o755)
	t.Setenv("PATH", bin)
}

// When codex mcp add fails, everything setup changed is put back.
func TestAFailedCodexAddPutsEverythingBack(t *testing.T) {
	home := pluginHome(t)
	seedPlugin(t, home, "mine")
	os.WriteFile(filepath.Join(codexHome(), "config.toml"), []byte("model = \"x\"\n"), 0o644)
	failingCodex(t, "mcp", "add")
	before := tree(t, home)
	if _, err := addToCodex("/x/jm"); err == nil {
		t.Fatal("set up anyway")
	}
	if after := tree(t, home); !reflect.DeepEqual(after, before) {
		t.Errorf("left changed:\n%v\nwas\n%v", after, before)
	}
}

// Removal leaves everything alone when the server isn't jm's, or Codex fails to remove the plugin.
func TestAFailedRemovalChangesNothing(t *testing.T) {
	home := pluginHome(t)
	seedPlugin(t, home, "mine")
	os.WriteFile(filepath.Join(codexHome(), "config.toml"), []byte("[mcp_servers.journeyman]\ncommand = \"/usr/bin/other\"\nargs = [\"mcp\"]\n"), 0o644)
	before := tree(t, home)
	if _, err := removeFromCodex(); err == nil || !reflect.DeepEqual(tree(t, home), before) {
		t.Errorf("a server that isn't jm's: %v", err)
	}
	os.WriteFile(filepath.Join(codexHome(), "config.toml"), []byte("[mcp_servers.journeyman]\ncommand = \"/x/jm\"\nargs = [\"mcp\"]\n\n[plugins.\"journeyman@mine\"]\nenabled = true\n"), 0o644)
	failingCodex(t, "plugin", "remove")
	before = tree(t, home)
	if _, err := removeFromCodex(); err == nil || !strings.Contains(err.Error(), "codex plugin remove") || !reflect.DeepEqual(tree(t, home), before) {
		t.Errorf("codex plugin remove failing: %v", err)
	}
}

// Another marketplace's Journeyman plugin (its table, its installed copy) is
// kept as it is through setup and removal, whatever the person's marketplace is called.
func TestOtherMarketplacesPluginsAreKept(t *testing.T) {
	for _, name := range []string{"mine", "renamed"} {
		home := pluginHome(t)
		seedPlugin(t, home, name)
		team := filepath.Join(codexHome(), "plugins", "cache", "team", "journeyman", "local", "sentinel")
		os.MkdirAll(filepath.Dir(team), 0o755)
		os.WriteFile(team, []byte("team"), 0o644)
		config := filepath.Join(codexHome(), "config.toml")
		before := "[plugins.\"journeyman@team\"]\nenabled = true\n\n[plugins.\"other@x\"]\nenabled = true\n"
		os.WriteFile(config, []byte(before), 0o644)
		if _, err := addToCodex("/x/jm"); err != nil {
			t.Fatal(err)
		}
		got, _ := os.ReadFile(config)
		if !strings.HasPrefix(string(got), before) || !strings.Contains(string(got), `[plugins."journeyman@`+name+`"]`) {
			t.Errorf("%s: config.toml:\n%s", name, got)
		}
		if _, err := removeFromCodex(); err != nil {
			t.Fatal(err)
		}
		if got, _ := os.ReadFile(config); string(got) != before {
			t.Errorf("%s: after removal:\n%s", name, got)
		}
		if data, _ := os.ReadFile(team); string(data) != "team" {
			t.Errorf("%s: the team's installed copy is gone", name)
		}
	}
}

// ChatGPT's setup says when the plugin couldn't be installed, as Codex's does.
func TestChatGPTSetupSaysThePluginFailed(t *testing.T) {
	home := pluginHome(t)
	os.MkdirAll(filepath.Join(home, "Applications", "ChatGPT.app"), 0o755)
	if !chatGPTDesktopFound() {
		t.Skip("ChatGPT Desktop is found by its app on macOS")
	}
	market := filepath.Join(home, ".agents", "plugins", "marketplace.json")
	os.MkdirAll(filepath.Dir(market), 0o755)
	os.WriteFile(market, []byte("{nope"), 0o644)
	if did, err := addToChatGPT("/x/jm"); err != nil || !strings.Contains(did, "its plugin wasn't") || !strings.Contains(did, market) {
		t.Errorf("%s %v", did, err)
	}
}

// Numbers in the marketplace keep their exact literals through setup and removal.
func TestMarketplaceNumbersStayExact(t *testing.T) {
	home := pluginHome(t)
	market := filepath.Join(home, ".agents", "plugins", "marketplace.json")
	os.MkdirAll(filepath.Dir(market), 0o755)
	numbers := []string{"9007199254740993", "18446744073709551615", "-42", "0.1", "1e300", "-2.5E-3"}
	os.WriteFile(market, []byte(`{"name": "mine", "plugins": [{"name": "other", "serial": [`+strings.Join(numbers, ", ")+`]}]}`), 0o644)
	for _, step := range []func() error{
		func() error {
			did, err := addToCodex("/x/jm")
			if err == nil && !strings.Contains(did, "and its plugin") {
				err = errors.New(did)
			}
			return err
		},
		func() error { _, err := removeFromCodex(); return err },
	} {
		if err := step(); err != nil {
			t.Fatal(err)
		}
		data, _ := os.ReadFile(market)
		for _, n := range numbers {
			if !strings.Contains(string(data), n) {
				t.Errorf("%s changed:\n%s", n, data)
			}
		}
	}
}
