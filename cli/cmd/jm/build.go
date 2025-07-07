package main

import (
	"encoding/json"
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

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build game assets and compile AssemblyScript",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("Building game...")

		defer cleanupTemp()

		manifestData, err := manifest.LoadManifest(".jm.json")
		exitOnError("Error loading manifest", err)

		std, err := stdlib.CreateStdLib()
		exitOnError("Failed to load stdlib", err)

		builder, err := docker.NewDockerBuilder("build")
		exitOnError("Failed to start build container", err)
		defer builder.Close()

		copyFileOrExit(".jm.json", filepath.Join("build", ".jm.json"))

		processAssets(manifestData.Assets, std, builder)
		processScenes(manifestData.Scenes)

		fmt.Println("Build complete!")
	},
}

func processAssets(assets []string, stdlib *stdlib.StdLib, builder *docker.DockerBuilder) {
	for _, asset := range assets {
		dst := filepath.Join("build", asset)
		copyFileOrExit(asset, dst)
		fmt.Printf("Copied asset: %s\n", asset)

		if strings.HasSuffix(asset, ".script.json") {
			buildScript(asset, stdlib, builder)
		}
	}
}

func buildScript(assetPath string, stdlib *stdlib.StdLib, builder *docker.DockerBuilder) {
	scriptAsset, err := manifest.LoadScriptAsset(assetPath)
	exitOnError(fmt.Sprintf("Failed to load script asset %s", assetPath), err)

	outputWasm := filepath.Join("build", scriptAsset.Binary)
	fmt.Printf("Building script: %s → %s\n", scriptAsset.Script, scriptAsset.Binary)

	userScriptData, err := os.ReadFile(scriptAsset.Script)
	exitOnError("Failed to read user script", err)

	mergedScript := stdlib.BuildScript(string(userScriptData))

	os.MkdirAll("_temp", os.ModePerm)
	tempPath := filepath.Join("_temp", filepath.Base(scriptAsset.Script))
	err = os.WriteFile(tempPath, []byte(mergedScript), 0644)
	exitOnError("Failed to write merged script", err)

	err = builder.BuildAssemblyScript(tempPath, outputWasm)
	exitOnError(fmt.Sprintf("Build failed for script %s", scriptAsset.Script), err)

	metadata, err := builder.BuildScriptMetaData(outputWasm)
	exitOnError(fmt.Sprintf("Build failed for script metadata %s", scriptAsset.Script), err)

	scriptAsset.Imports = metadata.Imports
	scriptAsset.Exposed = metadata.Exposed

	assetData, err := json.MarshalIndent(scriptAsset, "", "	")
	if err != nil {
		exitOnError("Failed to serialize script asset to JSON", err)
	}
	outPath := filepath.Join("build", assetPath)
	err = os.WriteFile(outPath, assetData, 0644)
	if err != nil {
		exitOnError(fmt.Sprintf("Failed to write script asset to %s", assetPath), err)
	}

	fmt.Printf("Built script: %s → %s\n", scriptAsset.Script, outputWasm)
}

func processScenes(scenes []string) {
	for _, scene := range scenes {
		dst := filepath.Join("build", scene)
		copyFileOrExit(scene, dst)
		fmt.Printf("Copied scene: %s\n", scene)
	}
}

func copyFileOrExit(src, dst string) {
	os.MkdirAll(filepath.Dir(dst), os.ModePerm)
	err := copyFile(src, dst)
	exitOnError(fmt.Sprintf("Failed to copy %s", src), err)
}

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

func exitOnError(msg string, err error) {
	if err != nil {
		fmt.Printf("%s: %s\n", msg, err)
		os.Exit(1)
	}
}

func cleanupTemp() {
	err := os.RemoveAll("_temp")
	if err != nil {
		fmt.Printf("Warning: failed to clean _temp directory: %s\n", err)
	}
}
