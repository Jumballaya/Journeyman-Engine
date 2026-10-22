package main

import (
	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
)

// The task board (jm tasks) as tools: what the person and the agent are doing
// to the game, each task linked to what it's about.
func (s *mcpServer) taskTools() []mcpTool {
	idArg := strArg("the task's id, e.g. t3 (tasks lists them)")
	columnArg := strArg("To do, Doing or Done")
	board := func(root string, text string) toolResult {
		columns, tasks, err := memory.Tasks(root)
		if err != nil {
			return textResult(text+err.Error(), true)
		}
		return toolResult{Text: text + boardText(columns, tasks), Structured: map[string]any{"columns": columns, "tasks": tasks}}
	}
	return []mcpTool{
		{Name: "tasks", Meta: callableMeta("", ""), Title: "The task board", Description: "The open game's task board (jm tasks): To do, Doing, Done; each task with what it links " +
			"(a design note, a game file, a play's marker jm://play/<id>/m<n>, an entity jm://entity?scene=…&name=…). Look before starting work the person asked for earlier.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}),
			onGame: func(root string, _ toolArgs) toolResult { return board(root, "") }},
		{Name: "task_add", Meta: callableMeta("Adding the task…", "Added"), Title: "Add a task",
			Description: "Put something to do on the board, e.g. a bug at the person's marker: link it (the marker's jm://play/<id>/m<n>, the entity, the file, the design note).",
			Annotations: writes(), InputSchema: object(map[string]any{"text": strArg("what to do, one line"), "links": listArg("what it's about: a note's title, a game file, jm:// links"),
				"column": columnArg}, "text"),
			onGame: func(root string, a toolArgs) toolResult {
				t, err := memory.AddTask(root, a.str("text"), a.str("column"), a.list("links"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return board(root, "Added "+t.ID+".\n")
			}},
		{Name: "task_move", Meta: callableMeta("Moving the task…", "Moved"), Title: "Move a task",
			Description: "Move a task: to Doing when you start it, Done when it's built, played and the person agrees.",
			Annotations: writes(), InputSchema: object(map[string]any{"id": idArg, "column": columnArg}, "id", "column"),
			onGame: func(root string, a toolArgs) toolResult {
				t, err := memory.MoveTask(root, a.str("id"), a.str("column"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return board(root, t.ID+" is in "+t.Column+".\n")
			}},
		{Name: "task_remove", Meta: callableMeta("Removing…", "Removed"), Title: "Remove a task",
			Description: "Take a task off the board (one that's no longer wanted; a finished one goes to Done instead).",
			Annotations: map[string]any{"readOnlyHint": false, "destructiveHint": true, "openWorldHint": false},
			InputSchema: object(map[string]any{"id": idArg}, "id"),
			onGame: func(root string, a toolArgs) toolResult {
				if err := memory.RemoveTask(root, a.str("id")); err != nil {
					return textResult(err.Error(), true)
				}
				return board(root, "Removed "+a.str("id")+".\n")
			}},
	}
}
