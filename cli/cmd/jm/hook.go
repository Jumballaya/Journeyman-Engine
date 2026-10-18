package main

import (
	"encoding/json"
	"fmt"
	"io"
	"path/filepath"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"

	"github.com/spf13/cobra"
)

// jm hook: what Journeyman's plugin runs at agent lifecycle events (its
// hooks/hooks.json); the agent app passes the event as JSON on stdin.
var hookCmd = &cobra.Command{Use: "hook", Short: "For Journeyman's agent plugin: its lifecycle hooks", Hidden: true}

func init() {
	hookCmd.AddCommand(&cobra.Command{Use: "session-start", Short: "Tells a new agent session which game its folder is", Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error { return sessionStart(cmd.InOrStdin(), cmd.OutOrStdout()) }})
}

// sessionStart says, when the session's folder is a game (or inside one),
// which game it is and how to start on it; nothing otherwise.
func sessionStart(in io.Reader, out io.Writer) error {
	var event struct {
		Cwd string `json:"cwd"`
	}
	_ = json.NewDecoder(in).Decode(&event) // no event: say nothing
	dir := gameAbove(event.Cwd)
	if dir == "" {
		return nil
	}
	text := fmt.Sprintf("This folder is the Journeyman game %s (%s). jm's MCP tools work on the open game: call open_game with %q first, "+
		"and read its AGENTS.md before changing it.", gameName(dir), dir, dir)
	if list, _ := plays.List(dir); len(list) > 0 {
		p := list[0]
		text += fmt.Sprintf(" The person's latest play is %s (%d markers): when they talk about playing, play_show it first.", p.ID, len(p.Meta.Markers))
	}
	text += keepInMind(dir)
	return json.NewEncoder(out).Encode(map[string]any{"hookSpecificOutput": map[string]any{
		"hookEventName": "SessionStart", "additionalContext": text}})
}

// gameAbove is the game folder dir is in (dir or a folder above it), "" if none.
func gameAbove(dir string) string {
	if dir == "" || !filepath.IsAbs(dir) {
		return ""
	}
	for {
		if exists(filepath.Join(dir, archive.ManifestEntryKey)) {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return ""
		}
		dir = parent
	}
}
