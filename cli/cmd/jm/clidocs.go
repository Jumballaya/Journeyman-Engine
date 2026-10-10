package main

//go:generate sh -c "go run . docs cli > ../../../docs/cli.md"

import (
	"fmt"
	"strings"

	"github.com/spf13/cobra"
	"github.com/spf13/pflag"
)

// cliReference is every command's help as one markdown page: `jm docs cli`, and docs/cli.md for the site.
func cliReference(root *cobra.Command) string {
	var b strings.Builder
	b.WriteString("# The jm CLI\n\nEvery command's `--help`, generated from jm itself (`jm docs cli`). `jm <command> --help` prints the same.\n")
	var walk func(c *cobra.Command)
	walk = func(c *cobra.Command) {
		for _, sub := range c.Commands() {
			if !sub.IsAvailableCommand() || sub.Name() == "completion" {
				continue
			}
			fmt.Fprintf(&b, "\n## %s\n\n%s.\n\n", sub.CommandPath(), strings.TrimSuffix(sub.Short, "."))
			b.WriteString(helpMarkdown(sub.Long))
			b.WriteString("```text\nUsage: " + sub.UseLine() + "\n")
			// Without help: cobra adds it only to commands that ran, so it'd make the page unstable.
			flags := pflag.NewFlagSet(sub.Name(), pflag.ContinueOnError)
			sub.NonInheritedFlags().VisitAll(func(f *pflag.Flag) {
				if f.Name != "help" {
					flags.AddFlag(f)
				}
			})
			if flags := flags.FlagUsages(); flags != "" {
				b.WriteString("\nFlags:\n" + flags)
			}
			b.WriteString("```\n")
			walk(sub)
		}
	}
	walk(root)
	return b.String()
}

// helpMarkdown turns a command's long help into markdown: its paragraphs as text,
// the indented blocks (examples, aligned lists) as code, so neither wraps twice.
func helpMarkdown(long string) string {
	var b strings.Builder
	for _, block := range strings.Split(strings.TrimSpace(long), "\n\n") {
		if strings.Contains("\n"+block, "\n  ") {
			b.WriteString("```text\n" + block + "\n```\n\n")
		} else {
			b.WriteString(strings.ReplaceAll(block, "<", "\\<") + "\n\n") // <Name> isn't an HTML tag
		}
	}
	return b.String()
}
