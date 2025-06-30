package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/docker"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
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
			fmt.Printf("Error loading manifest: %s\n", err)
			os.Exit(1)
		}

		// Step 1: Copy .jm.json
		dst := filepath.Join("build", ".jm.json")
		os.MkdirAll(filepath.Dir(dst), os.ModePerm)
		err = copyFile(".jm.json", dst)
		if err != nil {
			fmt.Printf("Failed to copy game manifest: %s\n", err)
			os.Exit(1)
		}

		// Step 2: Copy assets
		for _, asset := range man.Assets {
			dst := filepath.Join("build", asset)
			os.MkdirAll(filepath.Dir(dst), os.ModePerm)

			err := copyFile(asset, dst)
			if err != nil {
				fmt.Printf("Failed to copy asset %s: %s\n", asset, err)
				os.Exit(1)
			}
			fmt.Printf("Copied asset: %s\n", asset)

			// If this is a script JSON, we need to compile it
			if strings.HasSuffix(asset, ".script.json") {
				scriptAsset, err := manifest.LoadScriptAsset(asset)
				if err != nil {
					fmt.Printf("Failed to load script asset %s: %s\n", asset, err)
					os.Exit(1)
				}

				outputWasm := filepath.Join("build", scriptAsset.Binary)
				os.MkdirAll(filepath.Dir(outputWasm), os.ModePerm)

				fmt.Printf("Building script: %s → %s\n", scriptAsset.Script, scriptAsset.Binary)
				builder, err := docker.NewDockerBuilder()
				if err != nil {
					fmt.Printf("Failed to start build container: %s\n", err)
					os.Exit(1)
				}
				defer builder.Close()

				// For each script asset:
				err = builder.BuildAssemblyScript(scriptAsset.Script, outputWasm, "cli/runtime")
				if err != nil {
					fmt.Printf("Build failed for script %s: %s\n", scriptAsset.Script, err)
					os.Exit(1)
				}
				fmt.Printf("Built script: %s → %s\n", scriptAsset.Script, outputWasm)
			}
		}

		// Step 3: Copy scenes
		for _, scene := range man.Scenes {
			dst := filepath.Join("build", scene)
			os.MkdirAll(filepath.Dir(dst), os.ModePerm)

			err := copyFile(scene, dst)
			if err != nil {
				fmt.Printf("Failed to copy scene %s: %s\n", scene, err)
				os.Exit(1)
			}
			fmt.Printf("Copied scene: %s\n", scene)
		}

		fmt.Println("Assets processed and AssemblyScript build complete")
		fmt.Println("Build complete!")
	},
}
