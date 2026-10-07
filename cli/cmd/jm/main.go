package main

import (
	"errors"
	"fmt"
	"os"

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
	}
	rootCmd.AddCommand(runCmd, buildCmd, packCmd, migrateCmd, generateCmd, initCmd, exportCmd, testCmd)

	if err := rootCmd.Execute(); err != nil {
		fmt.Println(err)
		var me *migrateError
		if errors.As(err, &me) {
			os.Exit(me.code)
		}
		os.Exit(1)
	}
}
