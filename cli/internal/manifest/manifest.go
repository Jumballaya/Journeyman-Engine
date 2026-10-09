package manifest

import (
	"encoding/json"
	"os"
)

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
	// Multiplayer (engine/net): topology, port, playerPrefab, server... Passed
	// through to the engine; jm run --peers reads topology and port.
	Net map[string]interface{} `json:"net,omitempty"`
}

type ScriptAsset struct {
	Name    string   `json:"name"`
	Script  string   `json:"script"`
	Binary  string   `json:"binary"`
	Imports []string `json:"imports"`
	Exposed []string `json:"exposed"`
}

func LoadManifest(path string) (GameManifest, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return GameManifest{}, err
	}
	return LoadManifestFromBytes(data)
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
