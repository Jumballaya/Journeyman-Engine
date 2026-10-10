package main

import (
	"encoding/json"
	"fmt"

	"github.com/Jumballaya/Journeyman-Engine/internal/docs"

	"github.com/spf13/cobra"
)

var docsCmd = &cobra.Command{
	Use:   "docs [topic]",
	Short: "Print the engine's guides (scripting API, formats, testing, ...)",
	Long: `Without a topic, lists the guides; with one, prints it as markdown. They're
built into jm, so they match this version and need no network. --json lists
the topics as JSON.`,
	Args: cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		if len(args) == 1 && args[0] == "cli" {
			fmt.Fprint(cmd.OutOrStdout(), cliReference(cmd.Root()))
			return nil
		}
		if len(args) == 1 {
			text, err := docs.Read(args[0])
			if err != nil {
				return err
			}
			fmt.Fprint(cmd.OutOrStdout(), text)
			return nil
		}
		topics := append(docs.Topics(), docs.Topic{Name: "cli", Title: "The jm CLI"})
		if jsonOutput {
			out, _ := json.MarshalIndent(topics, "", "  ")
			fmt.Fprintln(cmd.OutOrStdout(), string(out))
			return nil
		}
		for _, t := range topics {
			fmt.Fprintf(cmd.OutOrStdout(), "%-18s %s\n", t.Name, t.Title)
		}
		fmt.Fprintln(cmd.OutOrStdout(), "\njm docs <topic> prints one.")
		return nil
	},
}

func init() {
	docsCmd.Flags().BoolVar(&jsonOutput, "json", false, "list the topics as JSON")
}
