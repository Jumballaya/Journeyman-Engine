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

	if err := rootCmd.Execute(); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
}
