// Package plugin is Journeyman's plugin for ChatGPT and Codex: jm's MCP
// server, its skills (onboarding first), a session-start hook and its marks,
// in the Agent Plugins layout (developers.openai.com/plugins/build/plugins).
package plugin

import (
	"embed"
	"encoding/json"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
)

//go:embed files
var Files embed.FS

// Name is the plugin's name, and its MCP server's.
const Name = "journeyman"

// OnboardingSkill is the skill hosts run right after installing the plugin.
const OnboardingSkill = "files/skills/onboarding/SKILL.md"

// Icon is jm's mark for a light or dark ("dark") background, as an SVG.
func Icon(theme string) []byte {
	name := "files/assets/icon.svg"
	if theme == "dark" {
		name = "files/assets/icon-dark.svg"
	}
	data, _ := Files.ReadFile(name)
	return data
}

// Options are what a written plugin runs: jm (its MCP server and hook run the
// jm at that path), jm's version, and the companions the person turned on.
type Options struct {
	JM, Version string
	Companions  []Companion
}

// Write puts the plugin in dir (replacing what's there).
func Write(dir string, o Options) error {
	if err := os.RemoveAll(dir); err != nil {
		return err
	}
	err := fs.WalkDir(Files, "files", func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() || strings.HasPrefix(path, "files/companions/") {
			return err
		}
		data, _ := Files.ReadFile(path)
		return writeFile(filepath.Join(dir, filepath.FromSlash(strings.TrimPrefix(path, "files/"))), data)
	})
	if err != nil {
		return err
	}
	command, err := launcher(dir, o.JM)
	if err != nil {
		return err
	}
	servers := map[string]any{Name: map[string]any{"type": "stdio", "command": command, "args": []string{"mcp"}}}
	for _, c := range o.Companions {
		skill, _ := Files.ReadFile("files/companions/" + c.Name + "/SKILL.md")
		if err := writeFile(filepath.Join(dir, "skills", c.Name, "SKILL.md"), skill); err != nil {
			return err
		}
		if c.Server != nil {
			servers[c.Name] = c.Server.json()
		}
	}
	jm := o.JM
	generated := map[string]any{
		"plugin.json": manifest(o.Version),
		"mcp.json":    map[string]any{"$schema": "https://agent-plugins.org/schemas/1.0.0/mcp.schema.json", "mcpServers": servers},
		"hooks/hooks.json": map[string]any{"hooks": map[string]any{"SessionStart": []any{map[string]any{
			"hooks": []any{map[string]any{"type": "command", "command": quote(jm) + " hook session-start"}}}}}},
	}
	for name, v := range generated {
		data, _ := json.MarshalIndent(v, "", "  ")
		if err := writeFile(filepath.Join(dir, filepath.FromSlash(name)), append(data, '\n')); err != nil {
			return err
		}
	}
	return nil
}

// launcher is the command mcp.json runs jm by: Codex takes a bare name (jm on
// PATH) or a path in the plugin, so a jm elsewhere is run by a script there.
func launcher(dir, jm string) (string, error) {
	if !strings.ContainsAny(jm, `/\`) {
		return jm, nil
	}
	name, script := "bin/jm", "#!/bin/sh\nexec "+quote(jm)+" \"$@\"\n"
	if runtime.GOOS == "windows" {
		name, script = "bin/jm.cmd", "@"+quote(jm)+" %*\r\n"
	}
	path := filepath.Join(dir, filepath.FromSlash(name))
	if err := writeFile(path, []byte(script)); err != nil {
		return "", err
	}
	return "./" + name, os.Chmod(path, 0o755)
}

func manifest(version string) map[string]any {
	return map[string]any{
		"$schema":     "https://agent-plugins.org/schemas/1.0.0/plugin.schema.json",
		"name":        Name,
		"version":     strings.TrimPrefix(version, "v"),
		"description": "Make and play 2D games with Journeyman: your games, scenes and recorded plays, beside the editor.",
		"author":      map[string]any{"name": "Journeyman"},
		"homepage":    "https://github.com/Jumballaya/Journeyman-Engine",
		"license":     "MIT",
		"keywords":    []string{"game", "2d", "game-engine", "gamedev"},
		"extensions": map[string]any{"com.openai": map[string]any{
			"onboardingSkill": "./" + strings.TrimPrefix(OnboardingSkill, "files/"),
			"interface": map[string]any{
				"displayName":      "Journeyman",
				"shortDescription": "Make and play 2D games beside the editor",
				"longDescription": "Your Journeyman games in ChatGPT: open, play and export them, see their scenes, " +
					"scrub your recorded plays with the agent, and change the game together while the editor stays open.",
				"developerName": "Journeyman",
				"category":      "Developer Tools",
				"capabilities":  []string{"Interactive", "Read", "Write"},
				"defaultPrompt": []string{"@Journeyman show me my games.", "@Journeyman look at my last play."},
				"websiteURL":    "https://github.com/Jumballaya/Journeyman-Engine",
				"brandColor":    "#E8590C",
				"composerIcon":  "./assets/icon.svg",
				"logo":          "./assets/icon.svg",
				"logoDark":      "./assets/icon-dark.svg",
			},
		}},
	}
}

func writeFile(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return atomicfile.WriteFile(path, data, 0o644)
}

// quote makes path one literal word for the shell that runs hook commands
// (cmd on Windows, which takes double quotes).
func quote(path string) string {
	if runtime.GOOS == "windows" {
		return `"` + path + `"`
	}
	return "'" + strings.ReplaceAll(path, "'", `'\''`) + "'"
}
