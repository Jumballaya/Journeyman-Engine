package main

import (
	"errors"
	"fmt"
	"os"
	"strings"

	"github.com/spf13/cobra"
)

// version is the release this jm belongs to, set when a release is built:
// go build -ldflags "-X main.version=v0.0.1". A local build says "dev".
var version = "dev"

func main() {
	rootCmd := &cobra.Command{
		Use:     "jm",
		Short:   "Journeyman CLI",
		Long:    "Journeyman CLI for managing, building and running games",
		Version: version,
		// A failed build or test ends with its error alone (printed once, below),
		// not the command's usage: that's for mistakes in the command line.
		SilenceUsage:  true,
		SilenceErrors: true,
	}
	rootCmd.AddCommand(runCmd, buildCmd, packCmd, migrateCmd, generateCmd, initCmd, exportCmd, testCmd, schemaCmd, goldenCmd, mcpCmd, fmtCmd, doctorCmd)
	// Mistakes in the command line itself point at the help.
	rootCmd.SetFlagErrorFunc(func(c *cobra.Command, err error) error {
		return fmt.Errorf("%w\nRun '%s --help' for usage.", err, c.CommandPath())
	})

	if err := rootCmd.Execute(); err != nil {
		fmt.Println(err)
		if strings.HasPrefix(err.Error(), "unknown command") {
			fmt.Println("Run 'jm --help' for usage.")
		}
		var me *migrateError
		if errors.As(err, &me) {
			os.Exit(me.code)
		}
		os.Exit(1)
	}
}
