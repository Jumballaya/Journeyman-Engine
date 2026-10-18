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
  jm memory search <query>          the notes a query names, best first: words
                                    (any form), "a phrase", type:character,
                                    a property (faction:dorne), +must -not
  jm memory note <text> [--in Levels] [--category feel]
                                    add an observation (default: to Notes)
  jm memory decide <title> <why>    record a decision (design/decisions/)
  jm memory kinds                   the kinds of dossier: character, item, location...
  jm memory world <note> [--steps 2] what's connected to a note, within steps
  jm memory path <from> <to>        how two notes connect
  jm memory relate <from> <type> <to> [--with key=value]...
                                    state a relation: "- type [[to]] (details)"
  jm memory dossier <kind> <name> [--summary text] [--set key=value]...
                                    a dossier (design/Characters/<name>.md...)
                                    from its kind's template

A kind is a template in design/_templates (Characters.md: its properties in
frontmatter, its sections in the body). Edit one, or add a file for a kind
of your own; design/<Folder>.base shows a kind as a table in Obsidian.`,
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

var (
	memoryNoteIn   string
	dossierSummary string
	dossierSet     []string
	worldSteps     int
	relateWith     []string
)

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
	sub("search <query>", "The notes a query names, best first", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
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
	sub("kinds", "The kinds of dossier and their properties", cobra.NoArgs, func(w io.Writer, root string, _ []string) error {
		kinds := memory.Kinds(root)
		if jsonOutput {
			return writeJSON(w, map[string]any{"kinds": kinds})
		}
		for _, k := range kinds {
			fmt.Fprintf(w, "%s (design/%s/): %s\n", k.Name, k.Folder, strings.Join(k.Props, ", "))
		}
		return nil
	})
	dossier := sub("dossier <kind> <name>", "A dossier from its kind's template", cobra.ExactArgs(2), func(w io.Writer, root string, args []string) error {
		props := map[string]string{}
		for _, kv := range dossierSet {
			key, value, ok := strings.Cut(kv, "=")
			if !ok || strings.TrimSpace(key) == "" {
				return fmt.Errorf("--set %q: give key=value", kv)
			}
			props[strings.TrimSpace(key)] = value
		}
		n, err := memory.NewDossier(root, args[0], args[1], dossierSummary, props)
		if err == nil {
			fmt.Fprintf(w, "Created %s\n", n.Path)
		}
		return err
	})
	world := sub("world <note>", "What's connected to a note in the game's world", cobra.ExactArgs(1), func(w io.Writer, root string, args []string) error {
		world := memory.WorldOf(root)
		near, edges, err := world.Around(args[0], worldSteps)
		if err != nil {
			return err
		}
		view := worldView(world, near, edges)
		if jsonOutput {
			return writeJSON(w, view.Structured)
		}
		fmt.Fprintln(w, view.Text)
		return nil
	})
	world.Flags().IntVar(&worldSteps, "steps", 2, "how far out")
	sub("path <from> <to>", "How two notes connect", cobra.ExactArgs(2), func(w io.Writer, root string, args []string) error {
		world := memory.WorldOf(root)
		chain, err := world.Path(args[0], args[1])
		if err != nil {
			return err
		}
		if jsonOutput {
			return writeJSON(w, map[string]any{"path": chain})
		}
		fmt.Fprintln(w, pathText(args[0], args[1], chain)+unread(world))
		return nil
	})
	relate := sub("relate <from> <type> <to>", "State a relation in a note", cobra.ExactArgs(3), func(w io.Writer, root string, args []string) error {
		details := map[string]string{}
		for _, kv := range relateWith {
			key, value, ok := strings.Cut(kv, "=")
			if !ok {
				return fmt.Errorf("--with %q: give key=value", kv)
			}
			details[strings.TrimSpace(key)] = value
		}
		n, err := memory.Relate(root, args[0], memory.Relation{Type: args[1], Target: args[2], Attrs: details})
		if err == nil {
			fmt.Fprintf(w, "Stated in %s\n", n.Path)
			for _, c := range memory.WorldOf(root).Conflicts() {
				fmt.Fprintf(w, "warning: %s: %s\n", c.Path, c.Message)
			}
		}
		return err
	})
	relate.Flags().StringArrayVar(&relateWith, "with", nil, `a detail, e.g. --with distance="120 km" (repeat)`)
	dossier.Flags().StringVar(&dossierSummary, "summary", "", "a line or two under its heading")
	dossier.Flags().StringArrayVar(&dossierSet, "set", nil, `a property, e.g. --set faction="[[House Dorne]]" (repeat)`)
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
