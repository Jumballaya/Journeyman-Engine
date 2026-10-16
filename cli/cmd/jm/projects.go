package main

import (
	"fmt"
	"path/filepath"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/projects"

	"github.com/spf13/cobra"
)

var projectsCmd = &cobra.Command{
	Use:   "projects",
	Short: "Your games: the ones you opened (the editor's recent projects) and the games folder's",
	Long: `Lists your games, newest opened first: the editor's recent projects (jm
and its MCP tools keep the same list), then the games folder's others
($JM_GAMES, else ~/Journeyman).

  jm projects                 the list (--json for tools)
  jm projects add <folder>    put a game on the list
  jm projects forget <folder> take one off it (its files stay)
  jm projects scan [folder]   add every game in a folder and the folders below it (default: this one)`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error { return emitProjects(cmd) },
}

func init() {
	projectsCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	sub := func(use, short string, args cobra.PositionalArgs, run func(cmd *cobra.Command, args []string) error) {
		projectsCmd.AddCommand(&cobra.Command{Use: use, Short: short, Args: args, RunE: run})
	}
	sub("list", "The list", cobra.NoArgs, func(cmd *cobra.Command, _ []string) error { return emitProjects(cmd) })
	sub("add <folder>", "Put a game on the list", cobra.ExactArgs(1), func(cmd *cobra.Command, args []string) error {
		dir, err := filepath.Abs(args[0])
		if err != nil {
			return err
		}
		if _, err := findGame(gamesHome(), dir); err != nil {
			return fmt.Errorf("%s isn't a game (no .jm.json): jm init makes one", dir)
		}
		if err := projects.Add(dir); err != nil {
			return err
		}
		return emitProjects(cmd)
	})
	sub("forget <folder>", "Take a game off the list (its files stay)", cobra.ExactArgs(1), func(cmd *cobra.Command, args []string) error {
		if err := projects.Forget(args[0]); err != nil {
			return err
		}
		return emitProjects(cmd)
	})
	sub("scan [folder]", "Add every game in a folder and the folders below it", cobra.MaximumNArgs(1), func(cmd *cobra.Command, args []string) error {
		folder := "."
		if len(args) == 1 {
			folder = args[0]
		}
		added, err := projects.Scan(folder)
		if err != nil {
			return err
		}
		if !jsonOutput {
			fmt.Fprintf(cmd.OutOrStdout(), "Added %d game(s).\n", len(added))
		}
		return emitProjects(cmd)
	})
}

func emitProjects(cmd *cobra.Command) error {
	list := projects.List(gamesHome())
	if jsonOutput {
		return writeJSON(cmd.OutOrStdout(), map[string]any{"projects": list})
	}
	w := cmd.OutOrStdout()
	if len(list) == 0 {
		fmt.Fprintln(w, "No games yet: jm init makes one.")
	}
	for _, p := range list {
		fmt.Fprintf(w, "%s  %s  %s\n", p.Name, p.Path, openedText(p))
	}
	return nil
}

// projectsText is the list for agents, each game with the name open_game takes for it.
func projectsText(list []projects.Project, home string) string {
	if len(list) == 0 {
		return "No games yet: new_game (or jm init) makes one, in " + home + "."
	}
	var b strings.Builder
	b.WriteString("Games, newest opened first:")
	for _, p := range list {
		fmt.Fprintf(&b, "\n  %s (open_game %q) %s", p.Name, gameRef(p.Path, home), openedText(p))
	}
	return b.String()
}

// gameRef is a games-folder game's folder name, else its path.
func gameRef(dir, home string) string {
	if filepath.Dir(dir) == evalOrSelf(home) {
		return filepath.Base(dir)
	}
	return dir
}

func evalOrSelf(dir string) string {
	if real, err := filepath.EvalSymlinks(dir); err == nil {
		return real
	}
	return dir
}

func openedText(p projects.Project) string {
	switch {
	case p.Missing:
		return "missing (moved or deleted)"
	case p.Opened == 0:
		return "never opened"
	}
	return "opened " + time.Unix(p.Opened, 0).Format("2006-01-02 15:04")
}
