package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/docker"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"

	"github.com/spf13/cobra"
)

// collectedScriptMetadata accumulates per-script metadata (imports, exposed)
// produced by the AssemblyScript compile path. Pack consumes this to populate
// resolver-entry metadata in archive mode. Reset at the start of each build
// invocation — necessary for tests that drive Run repeatedly in one process.
var collectedScriptMetadata = map[string]stdlib.ScriptMetaData{}

func recordScriptMetadata(tsPath string, md stdlib.ScriptMetaData) {
	collectedScriptMetadata[tsPath] = md
}

func resetScriptMetadata() {
	collectedScriptMetadata = map[string]stdlib.ScriptMetaData{}
}

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build game assets and compile AssemblyScript",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("Building game...")

		defer cleanupTemp()
		resetScriptMetadata()

		manifestData, err := manifest.LoadManifest(archive.ManifestEntryKey)
		exitOnError("Error loading manifest", err)

		// Pre-migration: scenes reference legacy `.script.json` paths but the
		// runtime now resolves scripts at `.ts` paths. Build a rewrite map from
		// `.script.json` entries in `assets[]` so processScenes can substitute
		// `.ts` references on the fly. Post-migration the map is empty and the
		// rewrite is a no-op.
		scriptJsonToTs := buildLegacyScriptMap(manifestData.Assets)

		std, err := stdlib.CreateStdLib()
		exitOnError("Failed to load stdlib", err)

		builder, err := docker.NewDockerBuilder("build")
		exitOnError("Failed to start build container", err)
		defer builder.Close()

		copyFileOrExit(archive.ManifestEntryKey, filepath.Join("build", archive.ManifestEntryKey))

		processAssets(manifestData.Assets, std, builder)
		processScenes(manifestData.Scenes, scriptJsonToTs)

		fmt.Println("Build complete!")
	},
}

// buildLegacyScriptMap reads each `.script.json` entry in `assets[]` and maps
// it to the `.ts` path it references. Used pre-migration to rewrite scenes
// that still point at `.script.json` files. Errors during read are skipped —
// processAssets surfaces the same error with better context.
func buildLegacyScriptMap(assets []string) map[string]string {
	m := map[string]string{}
	for _, a := range assets {
		if !strings.HasSuffix(a, ".script.json") {
			continue
		}
		data, err := os.ReadFile(a)
		if err != nil {
			continue
		}
		sa, err := manifest.LoadScriptAssetFromBytes(data)
		if err != nil {
			continue
		}
		key := filepath.ToSlash(filepath.Clean(a))
		val := filepath.ToSlash(filepath.Clean(sa.Script))
		m[key] = val
	}
	return m
}

func processAssets(assets []string, stdlib *stdlib.StdLib, builder *docker.DockerBuilder) {
	for _, asset := range assets {
		dst := filepath.Join("build", asset)
		copyFileOrExit(asset, dst)
		fmt.Printf("Copied asset: %s\n", asset)

		switch {
		case strings.HasSuffix(asset, ".ts"):
			buildScriptFromTs(asset, stdlib, builder)
		case strings.HasSuffix(asset, ".script.json"):
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

	// Mirror the wasm bytes at the `.ts` build path so the post-E.5 scene-rewrite
	// (which substitutes `.script.json` refs with their `.ts` source) resolves
	// against the same root regardless of which extension a scene references.
	// The legacy converter (`.script.json`) and the new converter (`.ts`) both
	// see the same wasm; either entry point loads the script identically.
	wasmBytes, err := os.ReadFile(outputWasm)
	exitOnError(fmt.Sprintf("Failed to re-read built wasm %s", outputWasm), err)
	tsArtifact := filepath.Join("build", filepath.FromSlash(scriptAsset.Script))
	os.MkdirAll(filepath.Dir(tsArtifact), os.ModePerm)
	err = os.WriteFile(tsArtifact, wasmBytes, 0644)
	exitOnError(fmt.Sprintf("Failed to write wasm at .ts artifact path %s", tsArtifact), err)
	recordScriptMetadata(filepath.ToSlash(filepath.Clean(scriptAsset.Script)), metadata)

	fmt.Printf("Built script: %s → %s\n", scriptAsset.Script, outputWasm)
}

func buildScriptFromTs(tsPath string, stdlib *stdlib.StdLib, builder *docker.DockerBuilder) {
	userScriptData, err := os.ReadFile(tsPath)
	exitOnError(fmt.Sprintf("Failed to read script source %s", tsPath), err)

	mergedScript := stdlib.BuildScript(string(userScriptData))

	os.MkdirAll("_temp", os.ModePerm)
	tempPath := filepath.Join("_temp", filepath.Base(tsPath))
	err = os.WriteFile(tempPath, []byte(mergedScript), 0644)
	exitOnError("Failed to write merged script", err)

	// Build artifact lands at the SAME path as source — `build/` is a separate
	// root, no collision. The bytes ARE wasm, overwriting the TypeScript that
	// `copyFileOrExit` placed there moments ago.
	outputWasm := filepath.Join("build", tsPath)
	err = builder.BuildAssemblyScript(tempPath, outputWasm)
	exitOnError(fmt.Sprintf("asc failed for %s", tsPath), err)

	metadata, err := builder.BuildScriptMetaData(outputWasm)
	exitOnError(fmt.Sprintf("metadata extraction failed for %s", tsPath), err)
	recordScriptMetadata(filepath.ToSlash(filepath.Clean(tsPath)), metadata)

	fmt.Printf("Built script: %s → %s (imports: %v)\n", tsPath, outputWasm, metadata.Imports)
}

func processScenes(scenes []string, scriptJsonToTs map[string]string) {
	for _, scene := range scenes {
		rewriteSceneFile(scene, scriptJsonToTs)
	}
}

func rewriteSceneFile(scenePath string, scriptJsonToTs map[string]string) {
	data, err := os.ReadFile(scenePath)
	exitOnError(fmt.Sprintf("read scene %s", scenePath), err)

	if len(scriptJsonToTs) == 0 {
		// No rewrites to perform — preserve byte-exact source so re-running
		// build is a stable no-op on the scene file.
		dst := filepath.Join("build", scenePath)
		os.MkdirAll(filepath.Dir(dst), os.ModePerm)
		err = os.WriteFile(dst, data, 0644)
		exitOnError(fmt.Sprintf("write scene %s", dst), err)
		fmt.Printf("Copied scene: %s\n", scenePath)
		return
	}

	var sceneJson map[string]interface{}
	if err := json.Unmarshal(data, &sceneJson); err != nil {
		exitOnError(fmt.Sprintf("parse scene %s", scenePath), err)
	}
	walkSceneAndRewrite(sceneJson, scriptJsonToTs, scenePath)
	out, err := json.MarshalIndent(sceneJson, "", "  ")
	exitOnError(fmt.Sprintf("marshal scene %s", scenePath), err)

	dst := filepath.Join("build", scenePath)
	os.MkdirAll(filepath.Dir(dst), os.ModePerm)
	err = os.WriteFile(dst, out, 0644)
	exitOnError(fmt.Sprintf("write scene %s", dst), err)
	fmt.Printf("Rewrote scene: %s\n", scenePath)
}

// walkSceneAndRewrite recursively descends `entities[].components.ScriptComponent.script`.
// In-place rewrites if the value is in the map. Logs (does not abort) for refs
// that aren't in the map — the user re-introduced a stale reference; engine
// will fail at scene load with "asset not found." Acceptable.
//
// @TODO: extend if more components reference scripts. Today's only script
// reference site is ScriptComponent.script.
func walkSceneAndRewrite(node interface{}, scriptJsonToTs map[string]string, scenePath string) {
	switch v := node.(type) {
	case map[string]interface{}:
		if comps, ok := v["components"].(map[string]interface{}); ok {
			if sc, ok := comps["ScriptComponent"].(map[string]interface{}); ok {
				if ref, ok := sc["script"].(string); ok && strings.HasSuffix(ref, ".script.json") {
					key := filepath.ToSlash(filepath.Clean(ref))
					if newRef, found := scriptJsonToTs[key]; found {
						sc["script"] = newRef
					} else {
						fmt.Printf("scene %s: no rewrite for %q (not in assets[])\n", scenePath, ref)
					}
				}
			}
		}
		for _, child := range v {
			walkSceneAndRewrite(child, scriptJsonToTs, scenePath)
		}
	case []interface{}:
		for _, child := range v {
			walkSceneAndRewrite(child, scriptJsonToTs, scenePath)
		}
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
