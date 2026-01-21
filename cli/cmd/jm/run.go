package main

import (
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"syscall"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

var runCmd = &cobra.Command{
	Use:   "run [build path or .jm archive]",
	Short: "Run the Journeyman game engine",
	Long:  "Run the game engine with a build directory or .jm archive",
	Args:  cobra.ExactArgs(1),
	Run: func(cmd *cobra.Command, args []string) {
		inputPath := args[0]
		var buildPath string
		var tempDir string
		var err error

		// Check if input is an archive
		if archive.IsArchive(inputPath) {
			fmt.Printf("Extracting archive: %s\n", inputPath)
			tempDir, err = os.MkdirTemp("", "jm-run-*")
			if err != nil {
				fmt.Printf("Failed to create temp directory: %s\n", err)
				os.Exit(1)
			}
			defer os.RemoveAll(tempDir)

			if err := archive.ExtractArchive(inputPath, tempDir); err != nil {
				fmt.Printf("Failed to extract archive: %s\n", err)
				os.Exit(1)
			}
			buildPath = tempDir
		} else {
			buildPath = inputPath
		}

		// Load the manifest
		manifestPath := filepath.Join(buildPath, ".jm.json")
		man, err := manifest.LoadManifest(manifestPath)
		if err != nil {
			fmt.Printf("Failed to load manifest from %s: %s\n", buildPath, err)
			os.Exit(1)
		}

		enginePath, err := resolveEnginePath(man.EnginePath, manifestPath)
		if err != nil {
			fmt.Printf("Engine binary not found: %s\n", err)
			os.Exit(1)
		}

		fmt.Printf("Running engine: %s with build: %s\n", enginePath, buildPath)

		// Create command
		engineCmd := exec.Command(enginePath, buildPath)
		engineCmd.Stdout = os.Stdout
		engineCmd.Stderr = os.Stderr
		engineCmd.Stdin = os.Stdin

		// Handle signals for graceful shutdown
		sigChan := make(chan os.Signal, 1)
		signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

		// Start the engine
		if err := engineCmd.Start(); err != nil {
			fmt.Printf("Error starting engine: %s\n", err)
			os.Exit(1)
		}

		// Wait for either the process to finish or a signal
		done := make(chan error, 1)
		go func() {
			done <- engineCmd.Wait()
		}()

		select {
		case err := <-done:
			if err != nil {
				fmt.Printf("Error running engine: %s\n", err)
				os.Exit(1)
			}
		case sig := <-sigChan:
			fmt.Printf("\nReceived signal: %v, terminating engine...\n", sig)
			if err := engineCmd.Process.Kill(); err != nil {
				fmt.Printf("Error killing engine process: %s\n", err)
			}
			os.Exit(1)
		}
	},
}

func resolveEnginePath(enginePath string, manifestPath string) (string, error) {
	if filepath.IsAbs(enginePath) {
		if _, err := os.Stat(enginePath); err == nil {
			return enginePath, nil
		}
		return "", fmt.Errorf("engine not found at absolute path: %s", enginePath)
	}

	manifestDir := filepath.Dir(manifestPath)
	fullPath := filepath.Join(manifestDir, enginePath)
	if _, err := os.Stat(fullPath); err == nil {
		return fullPath, nil
	}

	engineExec, err := exec.LookPath(enginePath)
	if err == nil {
		return engineExec, nil
	}

	return "", fmt.Errorf("could not resolve engine path: %s", enginePath)
}
