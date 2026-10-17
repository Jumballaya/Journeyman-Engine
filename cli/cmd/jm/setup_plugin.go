package main

import (
	"encoding/json"
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

// pluginMarketplace is the name of the person's marketplace (Codex's for a new
// one), or why jm can't list the plugin there; nothing changes.
func pluginMarketplace() (string, error) {
	if _, listable := listedPath(); !listable {
		return "", fmt.Errorf("%s isn't in your home folder, where the marketplace can list it", pluginSource())
	}
	_, name, err := readMarketplace(marketplaceFile(), personalMarketplace, "Personal")
	return name, err
}

// installPlugin writes the plugin for jm and lists it in marketplace (cached:
// also the installed copy Codex reads, where no codex CLI installs it). It
// gives back how to put all that back; failing, it has already.
func installPlugin(jm, marketplace string, cached bool) (undo, error) {
	rel, _ := listedPath()
	var u undo
	err := u.keep(pluginSource())
	if err == nil {
		err = plugin.Write(pluginSource(), jm, version)
	}
	if err == nil {
		err = u.keep(marketplaceFile())
	}
	if err == nil {
		_, err = editMarketplace(marketplaceFile(), personalMarketplace, "Personal", listing(rel, "INSTALLED_BY_DEFAULT"))
	}
	if err == nil && cached {
		if err = u.keep(pluginCache(marketplace)); err == nil {
			err = plugin.Write(pluginCache(marketplace), jm, version)
		}
	}
	if err != nil {
		u.run()
		return undo{}, err
	}
	return u, nil
}

// uninstallPlugin takes the plugin out: Codex's install of it (enabled is
// whether config.toml has it), its listing and its files; true: it was there.
// A failure leaves the rest as it was.
func uninstallPlugin(marketplace string, enabled bool) (undo, bool, error) {
	there := exists(pluginSource())
	_, listable := listedPath() // not listable: never listed
	var cache string
	if marketplace != "" { // unknown: jm never installed a copy it can name
		cache = filepath.Dir(pluginCache(marketplace))
	}
	var u undo
	err := u.keep(pluginSource())
	if err == nil && cache != "" {
		err = u.keep(cache) // before codex plugin remove deletes it
	}
	if err == nil && listable {
		err = u.keep(marketplaceFile())
	}
	if err == nil && enabled && hasCommand("codex")() {
		if out, cmdErr := exec.Command("codex", "plugin", "remove", pluginID(marketplace)).CombinedOutput(); cmdErr != nil {
			err = fmt.Errorf("codex plugin remove: %v: %s", cmdErr, strings.TrimSpace(string(out)))
		}
	}
	if err == nil && listable && exists(marketplaceFile()) {
		_, err = editMarketplace(marketplaceFile(), personalMarketplace, "Personal", withoutPlugin)
	}
	if err == nil && cache != "" {
		err = os.RemoveAll(cache)
	}
	if err == nil {
		err = os.RemoveAll(pluginSource())
	}
	if err != nil {
		u.run()
		return undo{}, there, err
	}
	return u, there, nil
}

// undo puts back, last first, what a setup changed when a later step fails
// (run), or drops what it kept once all went well (forget).
type undo struct{ back, drop []func() }

// keep saves path as it is (a file, a folder, or nothing) to put back; a
// folder is copied aside.
func (u *undo) keep(path string) error {
	info, err := os.Stat(path)
	switch {
	case os.IsNotExist(err):
		u.back = append(u.back, func() { os.RemoveAll(path) })
	case err != nil:
		return err
	case info.IsDir():
		aside := path + ".jm-undo"
		os.RemoveAll(aside)
		if err := os.CopyFS(aside, os.DirFS(path)); err != nil {
			os.RemoveAll(aside)
			return err
		}
		u.back = append(u.back, func() { os.RemoveAll(path); os.Rename(aside, path) })
		u.drop = append(u.drop, func() { os.RemoveAll(aside) })
	default:
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		u.back = append(u.back, func() {
			if now, err := os.Lstat(path); err == nil && now.IsDir() {
				os.RemoveAll(path)
			}
			atomicfile.WriteFile(path, data, info.Mode().Perm()) // through a link, as it was edited
		})
	}
	return nil
}

func (u undo) run() {
	for i := len(u.back) - 1; i >= 0; i-- {
		u.back[i]()
	}
}

func (u undo) forget() {
	for _, f := range u.drop {
		f()
	}
}

// then is u, followed by later's steps.
func (u undo) then(later undo) undo {
	return undo{append(u.back, later.back...), append(u.drop, later.drop...)}
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
	doc, name, err := readMarketplace(path, name, title)
	if err != nil {
		return "", err
	}
	plugins, _ := doc["plugins"].([]any)
	doc["plugins"] = change(plugins)
	data, _ := json.MarshalIndent(doc, "", "  ")
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return "", err
	}
	return name, atomicfile.WriteFile(path, append(data, '\n'), 0o644)
}

// readMarketplace is the marketplace at path (a new one, named name and title,
// if there's none) and its name, or why jm can't edit it. Its numbers stay as written.
func readMarketplace(path, name, title string) (map[string]any, string, error) {
	doc := map[string]any{"name": name, "interface": map[string]any{"displayName": title}, "plugins": []any{}}
	if data, err := os.ReadFile(path); err == nil {
		if decodeExactly(data, &doc) != nil {
			return nil, "", fmt.Errorf("%s isn't JSON jm can read: list the Journeyman plugin there by hand", path)
		}
	} else if !os.IsNotExist(err) {
		return nil, "", err
	}
	name, _ = doc["name"].(string)
	_, ok := doc["plugins"].([]any)
	if name == ".." || !marketplaceName.MatchString(name) || !ok && doc["plugins"] != nil {
		return nil, "", fmt.Errorf("%s has no name or plugins jm can read: list the Journeyman plugin there by hand", path)
	}
	return doc, name, nil
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
	if err := plugin.Write(source, "jm", version); err != nil {
		return false, err
	}
	_, err := editMarketplace(filepath.Join(dir, ".agents", "plugins", "marketplace.json"), "journeyman-game", "This game",
		listing(filepath.Join("plugins", plugin.Name), "AVAILABLE"))
	return true, err
}
