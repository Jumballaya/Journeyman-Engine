package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/plugin"
)

// The Journeyman plugin in Codex and ChatGPT Desktop (which shares Codex's
// settings): its files in ~/.codex/plugins/journeyman, listed in the person's
// marketplace (~/.agents/plugins/marketplace.json), installed and enabled.
// It carries jm's MCP server too; config.toml's own journeyman entry wins
// over a plugin's of the same name, so there's only ever one.

// personalMarketplace is Codex's name for a new personal marketplace.
const personalMarketplace = "codex-curated"

// listedPath is the plugin's folder as the marketplace lists it (from the
// home folder); false: it isn't in the home folder.
func listedPath() (string, bool) {
	rel, err := filepath.Rel(homeDir(), pluginSource())
	return rel, err == nil && rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator))
}

func pluginSource() string { return filepath.Join(codexHome(), "plugins", plugin.Name) }

func marketplaceFile() string {
	return filepath.Join(homeDir(), ".agents", "plugins", "marketplace.json")
}

// pluginID is the plugin as Codex names it: journeyman@<marketplace>.
func pluginID(marketplace string) string { return plugin.Name + "@" + marketplace }

// installPlugin writes the plugin for jm and lists it in the person's
// marketplace (cached: also the installed copy Codex reads, where no codex
// CLI installs it); it returns the marketplace's name.
func installPlugin(jm string, cached bool) (string, error) {
	rel, listable := listedPath()
	if !listable {
		return "", fmt.Errorf("%s isn't in your home folder, where the marketplace can list it", pluginSource())
	}
	if err := plugin.Write(pluginSource(), pluginOptions(jm)); err != nil {
		return "", err
	}
	name, err := editMarketplace(marketplaceFile(), personalMarketplace, "Personal", listing(rel, "INSTALLED_BY_DEFAULT"))
	if err == nil && cached {
		err = plugin.Write(pluginCache(name), pluginOptions(jm))
	}
	return name, err
}

// uninstallPlugin takes the plugin out: Codex's install of it, its listing and
// its files; true: it was there.
func uninstallPlugin() (bool, error) {
	there := exists(pluginSource())
	name := personalMarketplace
	if _, listable := listedPath(); listable && exists(marketplaceFile()) { // not listable: never listed
		var err error
		if name, err = editMarketplace(marketplaceFile(), personalMarketplace, "Personal", withoutPlugin); err != nil {
			return there, err
		}
	}
	if hasCommand("codex")() {
		_ = exec.Command("codex", "plugin", "remove", pluginID(name)).Run() // not installed is fine
	}
	return there, errors.Join(os.RemoveAll(filepath.Dir(pluginCache(name))), os.RemoveAll(pluginSource()))
}

// pluginCache is where Codex keeps an installed local plugin (its version "local").
func pluginCache(marketplace string) string {
	return filepath.Join(codexHome(), "plugins", "cache", marketplace, plugin.Name, "local")
}

func withoutPlugin(plugins []any) []any {
	return slices.DeleteFunc(plugins, func(p any) bool {
		entry, _ := p.(map[string]any)
		return entry["name"] == plugin.Name
	})
}

// listing puts the plugin at rel (from the marketplace's root) in a marketplace's plugins.
func listing(rel, installation string) func(plugins []any) []any {
	return func(plugins []any) []any {
		return append(withoutPlugin(plugins), map[string]any{"name": plugin.Name,
			"source":   map[string]any{"source": "local", "path": "./" + filepath.ToSlash(rel)},
			"policy":   map[string]any{"installation": installation, "authentication": "ON_INSTALL"},
			"category": "Developer Tools"})
	}
}

// editMarketplace changes the plugins of the marketplace at path (making it,
// named name and title, if there's none), keeping the rest; it returns its name.
func editMarketplace(path, name, title string, change func(plugins []any) []any) (string, error) {
	doc := map[string]any{"name": name, "interface": map[string]any{"displayName": title}, "plugins": []any{}}
	if data, err := os.ReadFile(path); err == nil {
		if json.Unmarshal(data, &doc) != nil {
			return "", fmt.Errorf("%s isn't JSON jm can read: list the Journeyman plugin there by hand", path)
		}
	} else if !os.IsNotExist(err) {
		return "", err
	}
	name, _ = doc["name"].(string)
	plugins, ok := doc["plugins"].([]any)
	if name == ".." || !marketplaceName.MatchString(name) || !ok && doc["plugins"] != nil {
		return "", fmt.Errorf("%s has no name or plugins jm can read: list the Journeyman plugin there by hand", path)
	}
	doc["plugins"] = change(plugins)
	data, _ := json.MarshalIndent(doc, "", "  ")
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return "", err
	}
	return name, atomicfile.WriteFile(path, append(data, '\n'), 0o644)
}

// marketplaceName is a name Codex takes (and one folder of its plugin cache).
var marketplaceName = regexp.MustCompile(`^[a-z0-9][a-z0-9._-]*$`)

// pluginTable enables the plugin in config.toml, as codex plugin add does.
func pluginTable(marketplace string) string {
	return fmt.Sprintf("[plugins.%s]\nenabled = true\n", tomlString(pluginID(marketplace)))
}

// addRepoPlugin gives the game in dir its own marketplace with the plugin
// (plugins/journeyman, running the jm on PATH), so Codex opened there offers
// it; false: the game has one already. Not installed by default: a person
// with jm set up has the plugin already.
func addRepoPlugin(dir string) (bool, error) {
	source := filepath.Join(dir, "plugins", plugin.Name)
	if exists(source) {
		return false, nil
	}
	if err := plugin.Write(source, plugin.Options{JM: "jm", Version: version}); err != nil {
		return false, err
	}
	_, err := editMarketplace(filepath.Join(dir, ".agents", "plugins", "marketplace.json"), "journeyman-game", "This game",
		listing(filepath.Join("plugins", plugin.Name), "AVAILABLE"))
	return true, err
}
