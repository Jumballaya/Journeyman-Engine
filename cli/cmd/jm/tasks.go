package main

import (
	"fmt"
	"io"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"

	"github.com/spf13/cobra"
)

var taskTo string
var taskLinks []string

var tasksCmd = &cobra.Command{
	Use:   "tasks",
	Short: "The game's task board (design/Tasks.md): what's to do, being done, done",
	Long: `The game's task board, kept in design/Tasks.md (Obsidian shows it with the
design notes): a column per heading (To do, Doing, Done), a task per line,
each with an id (^t3). A task links what it's about: a design note, a game
file, a marker of a recorded play (jm://play/<id>/m2) or an entity
(jm://entity?scene=…&name=…).

  jm tasks                                    the board (--json for tools)
  jm tasks add <text> [--link x]... [--to Doing]
  jm tasks move <id> <column>                 e.g. jm tasks move t3 Doing
  jm tasks done <id>
  jm tasks remove <id>`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		return inGameRoot(func(root string) error {
			columns, tasks, err := memory.Tasks(root)
			if err != nil {
				return err
			}
			if jsonOutput {
				return writeJSON(cmd.OutOrStdout(), map[string]any{"columns": columns, "tasks": tasks})
			}
			fmt.Fprint(cmd.OutOrStdout(), boardText(columns, tasks))
			return nil
		})
	},
}

func init() {
	tasksCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	sub := func(use, short string, args cobra.PositionalArgs, run func(w io.Writer, root string, args []string) error) *cobra.Command {
		c := &cobra.Command{Use: use, Short: short, Args: args, RunE: func(cmd *cobra.Command, args []string) error {
			return inGameRoot(func(root string) error { return run(cmd.OutOrStdout(), root, args) })
		}}
		tasksCmd.AddCommand(c)
		return c
	}
	add := sub("add <text>", "Add a task", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		t, err := memory.AddTask(root, args[0], taskTo, taskLinks)
		if err == nil {
			fmt.Fprintf(w, "Added %s to %s: %s\n", t.ID, t.Column, t.Text)
		}
		return err
	})
	add.Flags().StringVar(&taskTo, "to", "To do", "its column")
	add.Flags().StringArrayVar(&taskLinks, "link", nil, "what it's about: a note's title, a game file, a jm:// link (repeatable)")
	sub("move <id> <column>", "Move a task to a column", cobra.ExactArgs(2), func(w io.Writer, root string, args []string) error {
		t, err := memory.MoveTask(root, args[0], args[1])
		if err == nil {
			fmt.Fprintf(w, "%s is in %s.\n", t.ID, t.Column)
		}
		return err
	})
	sub("done <id>", "Move a task to Done", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		t, err := memory.MoveTask(root, args[0], "Done")
		if err == nil {
			fmt.Fprintf(w, "%s is done.\n", t.ID)
		}
		return err
	})
	sub("remove <id>", "Take a task off the board", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		return memory.RemoveTask(root, args[0])
	})
}

func boardText(columns []string, tasks []memory.Task) string {
	if len(tasks) == 0 {
		return "No tasks: jm tasks add makes one.\n"
	}
	var b strings.Builder
	for _, c := range columns {
		fmt.Fprintf(&b, "%s:\n", c)
		for _, t := range tasks {
			if t.Column == c {
				fmt.Fprintf(&b, "  %s  %s\n", t.ID, t.Text)
			}
		}
	}
	return b.String()
}
