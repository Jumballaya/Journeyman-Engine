package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/docker"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"

	"github.com/spf13/cobra"
)

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build game assets and compile AssemblyScript",
	Long:  "Build game assets including scripts, scenes, atlases, and optionally create a .jm archive",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("Building game...")

		defer cleanupTemp()

		manifestPath, _ := cmd.Flags().GetString("manifest")
		if manifestPath == "" {
			manifestPath = ".jm.json"
		}

		outputDir, _ := cmd.Flags().GetString("output")
		if outputDir == "" {
			outputDir = "build"
		}

		createArchive, _ := cmd.Flags().GetBool("archive")
		archivePath, _ := cmd.Flags().GetString("archive-output")

		manifestData, err := manifest.LoadManifest(manifestPath)
		exitOnError("Error loading manifest", err)

		std, err := stdlib.CreateStdLib()
		exitOnError("Failed to load stdlib", err)

		builder, err := docker.NewDockerBuilder(outputDir)
		exitOnError("Failed to start build container", err)
		defer builder.Close()

		// Copy manifest to build directory
		copyFileOrExit(manifestPath, filepath.Join(outputDir, ".jm.json"))

		// Process assets (scripts, atlases, etc.)
		processAssets(manifestData.Assets, std, builder, outputDir)

		// Process scenes
		processScenes(manifestData.Scenes, outputDir)

		fmt.Println("Build complete!")

		// Create archive if requested
		if createArchive {
			if archivePath == "" {
				archivePath = filepath.Join(outputDir, manifestData.Name+".jm")
			}
			fmt.Printf("Creating archive: %s\n", archivePath)
			if err := archive.CreateArchive(outputDir, archivePath); err != nil {
				exitOnError("Failed to create archive", err)
			}
			fmt.Printf("Archive created: %s\n", archivePath)
		}
	},
}

func init() {
	buildCmd.Flags().StringP("output", "o", "build", "Output directory for build artifacts")
	buildCmd.Flags().StringP("manifest", "m", ".jm.json", "Path to game manifest file")
	buildCmd.Flags().BoolP("archive", "a", false, "Create a .jm archive after building")
	buildCmd.Flags().String("archive-output", "", "Output path for archive (default: <output>/<game-name>.jm)")
}

func processAssets(assets []string, stdlib *stdlib.StdLib, builder *docker.DockerBuilder, outputDir string) {
	for _, asset := range assets {
		// Check if asset exists
		if _, err := os.Stat(asset); os.IsNotExist(err) {
			fmt.Printf("Warning: asset %s not found, skipping\n", asset)
			continue
		}

		dst := filepath.Join(outputDir, asset)
		copyFileOrExit(asset, dst)
		fmt.Printf("Copied asset: %s\n", asset)

		// Process script assets
		if strings.HasSuffix(asset, ".script.json") {
			buildScript(asset, stdlib, builder, outputDir)
		}

		// Process atlas configs
		if strings.HasSuffix(asset, ".atlas.json") {
			buildAtlas(asset, outputDir)
		}
	}
}

func buildScript(assetPath string, stdlib *stdlib.StdLib, builder *docker.DockerBuilder, outputDir string) {
	scriptAsset, err := manifest.LoadScriptAsset(assetPath)
	exitOnError(fmt.Sprintf("Failed to load script asset %s", assetPath), err)

	outputWasm := filepath.Join(outputDir, scriptAsset.Binary)
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
	outPath := filepath.Join(outputDir, assetPath)
	err = os.WriteFile(outPath, assetData, 0644)
	if err != nil {
		exitOnError(fmt.Sprintf("Failed to write script asset to %s", assetPath), err)
	}

	fmt.Printf("Built script: %s → %s\n", scriptAsset.Script, outputWasm)
}

func buildAtlas(assetPath string, outputDir string) {
	fmt.Printf("Building atlas: %s\n", assetPath)

	config, err := atlas.LoadAtlasConfig(assetPath)
	exitOnError(fmt.Sprintf("Failed to load atlas config %s", assetPath), err)

	// Build atlas (creates image and metadata)
	if err := atlas.BuildAtlas(config); err != nil {
		exitOnError(fmt.Sprintf("Failed to build atlas %s", assetPath), err)
	}

	// Copy atlas files to build directory
	if config.Output != "" {
		dstImage := filepath.Join(outputDir, config.Output)
		copyFileOrExit(config.Output, dstImage)
		fmt.Printf("Copied atlas image: %s\n", config.Output)
	}

	if config.OutputMetadata != "" {
		dstMetadata := filepath.Join(outputDir, config.OutputMetadata)
		copyFileOrExit(config.OutputMetadata, dstMetadata)
		fmt.Printf("Copied atlas metadata: %s\n", config.OutputMetadata)
	}

	fmt.Printf("Built atlas: %s\n", assetPath)
}

func processScenes(scenes []string, outputDir string) {
	for _, scene := range scenes {
		if _, err := os.Stat(scene); os.IsNotExist(err) {
			fmt.Printf("Warning: scene %s not found, skipping\n", scene)
			continue
		}
		dst := filepath.Join(outputDir, scene)
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
