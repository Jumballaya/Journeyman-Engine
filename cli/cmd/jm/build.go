package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/docker"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"

	"github.com/spf13/cobra"
)

func copyFile(src, dst string) error {
	input, err := os.Open(src)
	if err != nil {
		return err
	}
	defer input.Close()

	output, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer output.Close()

	_, err = io.Copy(output, input)
	return err
}

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build game assets and compile AssemblyScript",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("Building game...")

		man, err := manifest.LoadManifest(".jm.json")
		if err != nil {
			logFatal("Error loading manifest", err)
		}

		// Setup StdLib and Docker builder
		stdlib, err := stdlib.CreateStdLib()
		if err != nil {
			logFatal("Failed to load stdlib", err)
		}
		defer stdlib.Cleanup()

		builder, err := docker.NewDockerBuilder(stdlib.TempPath)
		if err != nil {
			logFatal("Failed to start build container", err)
		}
		defer builder.Close()

		// Copy the manifest
		copyOrExit(".jm.json", filepath.Join("build", ".jm.json"))

		// Copy assets and build scripts
		buildScriptAssets(man.Assets, stdlib, builder)

		// Copy scenes
		for _, scene := range man.Scenes {
			copyOrExit(scene, filepath.Join("build", scene))
			fmt.Printf("Copied scene: %s\n", scene)
		}

		fmt.Println("Build complete!")
	},
}

func buildScriptAssets(assets []string, stdlib *stdlib.StdLib, builder *docker.DockerBuilder) {
	for _, asset := range assets {
		dst := filepath.Join("build", asset)
		copyOrExit(asset, dst)
		fmt.Printf("Copied asset: %s\n", asset)

		if strings.HasSuffix(asset, ".script.json") {
			scriptAsset, err := manifest.LoadScriptAsset(asset)
			if err != nil {
				logFatal(fmt.Sprintf("Failed to load script asset %s", asset), err)
			}

			outputWasm := filepath.Join("build", scriptAsset.Binary)
			fmt.Printf("Building script: %s → %s\n", scriptAsset.Script, scriptAsset.Binary)

			// Merge prelude + user script
			userScriptData, err := os.ReadFile(scriptAsset.Script)
			if err != nil {
				logFatal("Failed to read user script", err)
			}

			mergedScript := stdlib.MergePrelude(string(userScriptData))
			os.MkdirAll("_temp", os.ModePerm)
			tempPath := filepath.Join("_temp", filepath.Base(scriptAsset.Script))
			err = os.WriteFile(tempPath, []byte(mergedScript), 0644)
			if err != nil {
				logFatal("Failed to write merged script", err)
			}

			err = builder.BuildAssemblyScript(tempPath, outputWasm)
			if err != nil {
				logFatal(fmt.Sprintf("Build failed for script %s", scriptAsset.Script), err)
			}

			fmt.Printf("Built script: %s → %s\n", scriptAsset.Script, outputWasm)
		}
	}
}

func copyOrExit(src, dst string) {
	os.MkdirAll(filepath.Dir(dst), os.ModePerm)
	err := copyFile(src, dst)
	if err != nil {
		logFatal(fmt.Sprintf("Failed to copy %s", src), err)
	}
}

func logFatal(msg string, err error) {
	fmt.Printf("%s: %s\n", msg, err)
	os.Exit(1)
}
