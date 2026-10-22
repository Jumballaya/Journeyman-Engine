package main

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/realpath"
)

// The game's log (engineLog, where its last run wrote it),
// for Studio's Console tab and the agent.
func (s *mcpServer) consoleTools() []mcpTool {
	return []mcpTool{{Name: "game_log", Title: "The game's log",
		Description: "The last lines of the open game's log (its latest run: played or tested), each with its level, " +
			"and how many warnings and errors there are. errors: only warnings and errors.",
		Annotations: readOnly(), Meta: callableMeta("", ""),
		InputSchema: object(map[string]any{"lines": intArg("how many of the last lines (default 200, at most 2000)"),
			"errors": boolArg("only warnings and errors")}),
		onGame: func(root string, a toolArgs) toolResult {
			game := root // whose log: a page following one game's can tell
			log := gameLog(root)
			if log == "" {
				return toolResult{Text: "No log yet: the game hasn't run here.", Structured: map[string]any{"lines": []logLine{}, "game": game}}
			}
			lines, counts := readLog(log, int(a.num("lines", 200, 1, 2000)), a.flag("errors"))
			var b strings.Builder
			_, rel, _ := realpath.Rel(root, log)
			fmt.Fprintf(&b, "%s: %d errors, %d warnings in all.\n", rel, counts["error"], counts["warning"])
			for _, l := range lines {
				fmt.Fprintf(&b, "%s %s\n", l.Level, l.Text)
			}
			return toolResult{Text: b.String(), Structured: map[string]any{"log": rel, "lines": lines, "counts": counts, "game": game}}
		}}}
}

// gameLog is the game's log (engineLog: its last run's), "" for none. Only a
// regular file in the game counts: a link out of it, or a folder or pipe, isn't its log.
func gameLog(root string) string {
	real, _, ok := realpath.Rel(root, filepath.FromSlash(engineLog))
	if info, err := os.Stat(real); !ok || err != nil || !info.Mode().IsRegular() {
		return ""
	}
	return real
}

type logLine struct {
	Time  string `json:"time"`
	Level string `json:"level"` // info, warning, error, debug...
	Text  string `json:"text"`
}

// logFormat is the logger's line: "[2026-10-08 09:47:04.080] [engine] [warning] text".
var logFormat = regexp.MustCompile(`^\[([^\]]+)\] \[[^\]]+\] \[([a-z]+)\] (.*)$`)

// readLog is the last n lines of log (warnings and errors only, with problems),
// and every level's count.
func readLog(log string, n int, problems bool) ([]logLine, map[string]int) {
	data, _ := os.ReadFile(log)
	counts := map[string]int{"error": 0, "warning": 0}
	lines := []logLine{}
	for _, raw := range strings.Split(strings.TrimRight(string(data), "\n"), "\n") {
		l := logLine{Level: "info", Text: raw}
		if m := logFormat.FindStringSubmatch(raw); m != nil {
			l = logLine{Time: m[1], Level: strings.Replace(m[2], "critical", "error", 1), Text: m[3]}
		}
		counts[l.Level]++
		if !problems || l.Level == "error" || l.Level == "warning" {
			lines = append(lines, l)
		}
	}
	return lines[max(0, len(lines)-n):], counts
}
