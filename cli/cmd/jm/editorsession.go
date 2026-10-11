package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"slices"
	"time"
)

// editorUnsaved are the project's files with unsaved edits in an editor, sorted, from
// the .jm/editor-session-<pid>.json each running editor keeps current. One not
// rewritten within staleAfter (either way: clocks move) is ignored: that editor
// quit or crashed. The editor that started this jm (JM_EDITOR_PID) doesn't count:
// the person knows their own edits.
func editorUnsaved(projectRoot string) []string {
	const staleAfter = 15 * time.Second // editors rewrite theirs every 5 s
	sessions, _ := filepath.Glob(filepath.Join(projectRoot, ".jm", "editor-session-*.json"))
	own := "editor-session-" + os.Getenv("JM_EDITOR_PID") + ".json"
	var unsaved []string
	for _, path := range sessions {
		if filepath.Base(path) == own {
			continue
		}
		var session struct {
			Unsaved []string `json:"unsaved"`
			Updated int64    `json:"updated"` // Unix seconds
		}
		data, err := os.ReadFile(path)
		if err != nil || json.Unmarshal(data, &session) != nil {
			continue
		}
		if age := time.Since(time.Unix(session.Updated, 0)); age > staleAfter || age < -staleAfter {
			continue
		}
		unsaved = append(unsaved, session.Unsaved...)
	}
	slices.Sort(unsaved) // two editors can have the same file unsaved
	return slices.Compact(unsaved)
}

// warnAboutEditorEdits tells an agent which files the editor has unsaved edits
// in: changing one now loads as "Change on Disk" there, the person's edits one Undo back.
func warnAboutEditorEdits(projectRoot string) {
	for _, file := range editorUnsaved(projectRoot) {
		emit(Diagnostic{Level: "warning", Category: "editor", File: file,
			Message: "open in the editor with unsaved edits: change it and the editor loads your version, its edits one Undo back; ask the person to save first"})
	}
}
