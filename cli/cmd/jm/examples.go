package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/Jumballaya/Journeyman-Engine/internal/examples"

	"github.com/spf13/cobra"
)

var examplesCmd = &cobra.Command{
	Use:   "examples",
	Short: "The example games: list them, or start a game from one",
	Long: `The example games of this jm's release (the repo's demos/): the first use
downloads them (about 15 MB) into ~/.jm/examples/<version>.

  jm examples                      the examples
  jm examples copy <name> [dir]    a new game in dir (default: ./<name>) from one,
                                   yours to change; then jm build && jm run`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error { return listExamples(cmd) },
}

func init() {
	examplesCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	examplesCmd.AddCommand(&cobra.Command{
		Use: "list", Short: "The examples", Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error { return listExamples(cmd) },
	}, &cobra.Command{
		Use: "copy <name> [dir]", Short: "Start a new game from an example", Args: cobra.RangeArgs(1, 2),
		RunE: func(cmd *cobra.Command, args []string) error {
			dir := arg(args, 1)
			if dir == "" {
				dir = args[0]
			}
			if err := examples.Copy(version, args[0], dir, os.Stderr); err != nil {
				return err
			}
			abs, _ := filepath.Abs(dir)
			fmt.Fprintf(cmd.OutOrStdout(), "Made %s from the %s example. Next: cd %q && jm build && jm run\n", abs, args[0], dir)
			return nil
		},
	})
}

func listExamples(cmd *cobra.Command) error {
	list, err := examples.List(version, os.Stderr)
	if err != nil {
		return err
	}
	if jsonOutput {
		return writeJSON(cmd.OutOrStdout(), list)
	}
	for _, ex := range list {
		fmt.Fprintf(cmd.OutOrStdout(), "%-14s %s\n", ex.Name, ex.Title)
	}
	return nil
}
