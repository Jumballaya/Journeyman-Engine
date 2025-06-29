package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/spf13/cobra"
)

var runCmd = &cobra.Command{
	Use:   "run [build path]",
	Short: "Run the Journeyman game engine",
	Args:  cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		buildPath := args[0]
		enginePath := filepath.Join("build", "engine", "journeyman_engine")

		if _, err := os.Stat(enginePath); os.IsNotExist(err) {
			fmt.Printf("Engine binary not found at: %s\n", enginePath)
			os.Exit(1)
		}

		fmt.Printf("Running engine: %s with build: %s\n", enginePath, buildPath)

		engineCmd := exec.Command(enginePath, buildPath)
		engineCmd.Stdout = os.Stdout
		engineCmd.Stderr = os.Stderr

		if err := engineCmd.Run(); err != nil {
			fmt.Printf("Error running engine: %s\n", err)
			os.Exit(1)
		}
	},
}
