package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"time"
)

// editorSession is a running editor on the project, as the
// .jm/editor-session-<pid>.json it keeps current says (editor/EditorSession.hpp).
type editorSession struct {
	Pid       string       `json:"pid"`
	Open      []string     `json:"open"`
	Unsaved   []string     `json:"unsaved"`
	Scene     string       `json:"scene"`     // the scene being edited, "" for none
	Selection []string     `json:"selection"` // its selected entities' names
	Notes     []editorNote `json:"notes"`     // what the person pointed out to their agent, oldest first
	Updated   int64        `json:"updated"`   // Unix seconds
	file      string
}

type editorNote struct {
	ID       int      `json:"id"`
	At       int64    `json:"at"` // Unix seconds
	Scene    string   `json:"scene"`
	Entities []string `json:"entities"`
	Text     string   `json:"text"`
}

// liveEditors are the editors running on projectRoot, the latest written
// first. A session not rewritten within staleAfter (either way: clocks move)
// is ignored: that editor quit or crashed.
func liveEditors(projectRoot string) []editorSession {
	const staleAfter = 15 * time.Second // editors rewrite theirs every 5 s
	files, _ := filepath.Glob(filepath.Join(projectRoot, ".jm", "editor-session-*.json"))
	var live []editorSession
	for _, path := range files {
		var session editorSession
		data, err := os.ReadFile(path)
		if err != nil || json.Unmarshal(data, &session) != nil {
			continue
		}
		if age := time.Since(time.Unix(session.Updated, 0)); age > staleAfter || age < -staleAfter {
			continue
		}
		session.Pid = strings.TrimSuffix(strings.TrimPrefix(filepath.Base(path), "editor-session-"), ".json")
		session.file = path
		live = append(live, session)
	}
	slices.SortStableFunc(live, func(a, b editorSession) int { return int(b.Updated - a.Updated) })
	return live
}

// show asks the editor to open scene (if given), select and frame entity (by
// name, if given) and show note.
func (e editorSession) show(scene, entity, note string) error {
	line, _ := json.Marshal(map[string]string{"scene": scene, "entity": entity, "note": note})
	inbox, err := os.OpenFile(strings.TrimSuffix(e.file, ".json")+".in", os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return fmt.Errorf("couldn't reach the editor: %w", err)
	}
	defer inbox.Close()
	_, err = inbox.Write(append(line, '\n')) // one write: the editor never reads half a line as whole
	return err
}

// editorUnsaved are the project's files with unsaved edits in an editor,
// sorted. The editor that started this jm (JM_EDITOR_PID) doesn't count: the
// person knows their own edits.
func editorUnsaved(projectRoot string) []string {
	var unsaved []string
	for _, session := range liveEditors(projectRoot) {
		if session.Pid != os.Getenv("JM_EDITOR_PID") {
			unsaved = append(unsaved, session.Unsaved...)
		}
	}
	slices.Sort(unsaved) // two editors can have the same file unsaved
	return slices.Compact(unsaved)
}

// warnAboutEditorEdits tells an agent which files the editor has unsaved edits
// in: changing one now loads as "Change on Disk" there (a scene or prefab merges entity by entity).
func warnAboutEditorEdits(r *report, projectRoot string) {
	for _, file := range editorUnsaved(projectRoot) {
		r.add(Diagnostic{Level: "warning", Category: "editor", File: file,
			Message: "open in the editor with unsaved edits: a scene or prefab merges your change entity by entity (one you both changed takes yours), other files take your version, their edits one Undo back; ask the person to save first"})
	}
}
