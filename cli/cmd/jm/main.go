package main

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
)

func main() {
	var rootCmd = &cobra.Command{
		Use:   "jm",
		Short: "Journeyman CLI",
		Long:  "Journmeyman CLI for managing, building and running games",
	}

	rootCmd.AddCommand(runCmd)
	rootCmd.AddCommand(buildCmd)
	rootCmd.AddCommand(packCmd)
	rootCmd.AddCommand(migrateCmd)
	rootCmd.AddCommand(generateCmd)
	rootCmd.AddCommand(initCmd)

	if err := rootCmd.Execute(); err != nil {
		fmt.Println(err)
		var me *migrateError
		if errorsAs(err, &me) {
			os.Exit(int(me.code))
		}
		os.Exit(1)
	}
}

// errorsAs is a thin wrapper so main.go doesn't import "errors" alongside the
// other transitive deps. Keeps the import list tight.
func errorsAs(err error, target interface{}) bool {
	me, ok := target.(**migrateError)
	if !ok {
		return false
	}
	for {
		if e, ok := err.(*migrateError); ok {
			*me = e
			return true
		}
		u, ok := err.(interface{ Unwrap() error })
		if !ok {
			return false
		}
		err = u.Unwrap()
		if err == nil {
			return false
		}
	}
}
