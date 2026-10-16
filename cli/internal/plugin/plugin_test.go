package plugin

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func readJSON(t *testing.T, path string) map[string]any {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var v map[string]any
	if err := json.Unmarshal(data, &v); err != nil {
		t.Fatalf("%s: %v", path, err)
	}
	return v
}

// What Codex checks when it loads an Agent Plugins manifest, and every path
// the manifest names is in the plugin.
func TestWrittenPluginIsWhatCodexLoads(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "stale.txt"), nil, 0o644)
	if err := Write(dir, Options{JM: `/Apps/J M/jm`, Version: "v0.0.8"}); err != nil {
		t.Fatal(err)
	}
	if exists(filepath.Join(dir, "stale.txt")) {
		t.Error("a file from before survived")
	}
	m := readJSON(t, filepath.Join(dir, "plugin.json"))
	if m["$schema"] != "https://agent-plugins.org/schemas/1.0.0/plugin.schema.json" || m["name"] != Name || m["version"] != "0.0.8" {
		t.Errorf("manifest: %v", m)
	}
	openai := m["extensions"].(map[string]any)["com.openai"].(map[string]any)
	paths := []string{openai["onboardingSkill"].(string)}
	for _, key := range []string{"composerIcon", "logo", "logoDark"} {
		paths = append(paths, openai["interface"].(map[string]any)[key].(string))
	}
	for _, p := range paths {
		if !strings.HasPrefix(p, "./") || !exists(filepath.Join(dir, p)) {
			t.Errorf("the manifest names %s, not in the plugin", p)
		}
	}
	mcp := readJSON(t, filepath.Join(dir, "mcp.json"))
	if mcp["$schema"] != "https://agent-plugins.org/schemas/1.0.0/mcp.schema.json" { // Codex refuses one without
		t.Errorf("mcp.json: %v", mcp)
	}
	server := mcp["mcpServers"].(map[string]any)[Name].(map[string]any)
	if server["type"] != "stdio" || server["command"] != "/Apps/J M/jm" || server["args"].([]any)[0] != "mcp" {
		t.Errorf("server: %v", server)
	}
	hook := readJSON(t, filepath.Join(dir, "hooks", "hooks.json"))["hooks"].(map[string]any)["SessionStart"].([]any)[0].(map[string]any)["hooks"].([]any)[0].(map[string]any)
	if want := `'/Apps/J M/jm' hook session-start`; runtime.GOOS != "windows" && hook["command"] != want {
		t.Errorf("hook: %v", hook)
	}
	skills, _ := filepath.Glob(filepath.Join(dir, "skills", "*", "SKILL.md"))
	for _, s := range skills {
		data, _ := os.ReadFile(s)
		name := filepath.Base(filepath.Dir(s))
		if !strings.HasPrefix(string(data), "---\nname: "+name+"\ndescription: ") {
			t.Errorf("%s: frontmatter doesn't name it %s", s, name)
		}
	}
	if len(skills) < 4 {
		t.Errorf("skills: %v", skills)
	}
}

func exists(path string) bool { _, err := os.Stat(path); return err == nil }
