package main

import (
	"fmt"
	"io"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"

	"github.com/spf13/cobra"
)

var memoryCategory string

var memoryCmd = &cobra.Command{
	Use:   "memory",
	Short: "The game's design notes and decisions (design/)",
	Long: `The game's memory: design/, Markdown notes on what the game is and why
(Pitch, Pillars, Levels, Systems, Decisions, Notes), for the person and their
agent. Obsidian opens design/ as a vault; the notes are also Basic Memory's
format (a note per thing, "- [category] text" observations, "- kind [[Note]]"
relations), so it can index them too. [[Note]] links a note; a game file is
named in backticks (` + "`scenes/main.scene.json`" + `).

  jm memory                         the notes (--json for tools)
  jm memory init                    make design/ (jm init does, for new games)
  jm memory show <note>             a note, what it links and what links it
  jm memory search <text>           notes whose title or lines hold text
  jm memory note <text> [--in Levels] [--category feel]
                                    add an observation (default: to Notes)
  jm memory decide <title> <why>    record a decision (design/decisions/)`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		return inGameRoot(func(root string) error {
			notes := memory.Notes(root)
			if jsonOutput {
				return writeJSON(cmd.OutOrStdout(), map[string]any{"notes": notes})
			}
			if len(notes) == 0 {
				fmt.Fprintln(cmd.OutOrStdout(), "No design notes yet: jm memory init makes them.")
			}
			for _, n := range notes {
				fmt.Fprintf(cmd.OutOrStdout(), "%s  (%s, %d observations)  %s\n", n.Title, n.Type, len(n.Observations), n.Path)
			}
			return nil
		})
	},
}

var memoryNoteIn string

func init() {
	memoryCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	sub := func(use, short string, args cobra.PositionalArgs, run func(w io.Writer, root string, args []string) error) *cobra.Command {
		c := &cobra.Command{Use: use, Short: short, Args: args, RunE: func(cmd *cobra.Command, args []string) error {
			return inGameRoot(func(root string) error { return run(cmd.OutOrStdout(), root, args) })
		}}
		memoryCmd.AddCommand(c)
		return c
	}
	sub("init", "Make design/ (kept where it exists)", cobra.NoArgs, func(w io.Writer, root string, _ []string) error {
		made, err := memory.Scaffold(root)
		for _, p := range made {
			fmt.Fprintln(w, "Created", p)
		}
		return err
	})
	sub("show <note>", "A note, what it links and what links it", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		n, ok := memory.Find(root, args[0])
		if !ok {
			return fmt.Errorf("no note %q (jm memory lists them)", args[0])
		}
		if jsonOutput {
			return writeJSON(w, noteView(root, n))
		}
		fmt.Fprintf(w, "%s\n--\nLinks: %s\nLinked from: %s\n", strings.TrimRight(n.Text, "\n"),
			strings.Join(n.Links, ", "), strings.Join(memory.Backlinks(root, n), ", "))
		return nil
	})
	sub("search <text>", "Notes whose title or lines hold text (any case)", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		hits := memory.Search(root, args[0])
		if jsonOutput {
			return writeJSON(w, map[string]any{"hits": hits})
		}
		for _, h := range hits {
			if h.Line == 0 {
				fmt.Fprintf(w, "%s (%s)\n", h.Title, h.Path)
			} else {
				fmt.Fprintf(w, "%s:%d: %s\n", h.Path, h.Line, h.Text)
			}
		}
		return nil
	})
	note := sub("note <text>", "Add an observation to a note (default: Notes)", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		n, err := memory.Observe(root, memoryNoteIn, memoryCategory, args[0])
		if err == nil {
			fmt.Fprintf(w, "Noted in %s (%s)\n", n.Title, n.Path)
		}
		return err
	})
	note.Flags().StringVar(&memoryNoteIn, "in", "Notes", "the note to add to (made if there's none of that name)")
	note.Flags().StringVar(&memoryCategory, "category", "note", "what kind of observation, e.g. feel, idea, bug, playtest")
	sub("decide <title> <why>", "Record a decision and why (design/decisions/)", cobra.ExactArgs(2), func(w io.Writer, root string, args []string) error {
		n, err := memory.Decide(root, args[0], args[1])
		if err == nil {
			fmt.Fprintf(w, "Decided: %s (%s)\n", n.Title, n.Path)
		}
		return err
	})
}

func inGameRoot(run func(root string) error) error {
	root, err := projectRoot()
	if err != nil {
		return err
	}
	return run(root)
}

// noteView is a note as tools show it: its text, what it links and what
// links it, and a link that opens it in Obsidian.
func noteView(root string, n memory.Note) map[string]any {
	return map[string]any{"note": n, "text": n.Text, "backlinks": memory.Backlinks(root, n), "obsidian": memory.ObsidianURL(root, n)}
}
