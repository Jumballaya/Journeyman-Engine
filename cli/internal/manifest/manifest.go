package manifest

import (
	"encoding/json"
	"os"
)

type GameManifest struct {
	Name       string   `json:"name"`
	Version    string   `json:"version"`
	EntryScene string   `json:"entryString"`
	Scenes     []string `json:"scenes"`
	Assets     []string `json:"assets"`
}

type ScriptAsset struct {
	Name   string `json:"name"`
	Script string `json:"script"`
	Binary string `json:"binary"`
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
