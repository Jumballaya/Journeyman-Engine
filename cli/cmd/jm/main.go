package main

import (
	"errors"
	"fmt"
	"os"

	"github.com/spf13/cobra"
)

func main() {
	rootCmd := &cobra.Command{
		Use:   "jm",
		Short: "Journeyman CLI",
		Long:  "Journeyman CLI for managing, building and running games",
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
