package manifest

import (
	"encoding/json"
	"os"
	"strings"
)

// IsScriptSource reports whether `path` is a user-authored script source. Used
// by the build dispatcher to route `.ts` files through the AssemblyScript
// compile path, and by `jm migrate` to detect bare `.ts` references.
func IsScriptSource(path string) bool {
	return strings.HasSuffix(path, ".ts")
}

type GameManifest struct {
	Name       string   `json:"name"`
	Version    string   `json:"version"`
	EnginePath string   `json:"engine"`
	EntryScene string   `json:"entryScene"`
	Scenes     []string `json:"scenes"`
	// Files to ship: paths, or globs ("assets/prefabs/*.prefab.json",
	// "assets/maps/**") that jm build expands; see ExpandAssets.
	Assets []string `json:"assets"`
	// Shared script packages: import name -> folder (relative to the project),
	// copied into assets/scripts/node_modules/<name> by jm build and jm test.
	ScriptLibraries map[string]string `json:"scriptLibraries,omitempty"`
	// Engine/module settings (window, renderer, ui, ...). Passed through to
	// the engine untouched; the CLI only reads it to name exported apps.
	Config map[string]interface{} `json:"config,omitempty"`
}

type ScriptAsset struct {
	Name    string   `json:"name"`
	Script  string   `json:"script"`
	Binary  string   `json:"binary"`
	Imports []string `json:"imports"`
	Exposed []string `json:"exposed"`
}

func LoadManifest(path string) (GameManifest, error) {
	var manifest GameManifest

	data, err := os.ReadFile(path)
	if err != nil {
		return manifest, err
	}

	err = json.Unmarshal(data, &manifest)
	return manifest, err
}

func LoadScriptAsset(path string) (ScriptAsset, error) {
	var script ScriptAsset

	data, err := os.ReadFile(path)
	if err != nil {
		return script, err
	}

	err = json.Unmarshal(data, &script)
	return script, err
}

func LoadManifestFromBytes(data []byte) (GameManifest, error) {
	var manifest GameManifest
	err := json.Unmarshal(data, &manifest)
	return manifest, err
}

func LoadScriptAssetFromBytes(data []byte) (ScriptAsset, error) {
	var script ScriptAsset
	err := json.Unmarshal(data, &script)
	return script, err
}
