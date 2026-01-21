package docker

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"
	"github.com/docker/docker/api/types/container"
	"github.com/testcontainers/testcontainers-go"
	"github.com/testcontainers/testcontainers-go/wait"
)

const fileModeUserRW = 0644

// importRegex matches import declarations in wasm-objdump output.
// Format: <env.functionName> where functionName is the imported function.
// Example matches:
//   - <env.__jmLog> -> "__jmLog"
//   - <env.get_component> -> "get_component"
//   - <env.__jmPlaySound> -> "__jmPlaySound"
var importRegex = regexp.MustCompile(`<env\.([a-zA-Z0-9_]+)>`)

type DockerBuilder struct {
	Container   testcontainers.Container
	RuntimePath string
}

func NewDockerBuilder(runtimePath string) (*DockerBuilder, error) {
	err := os.MkdirAll(runtimePath, os.ModePerm)
	if err != nil {
		return nil, fmt.Errorf("failed to create build directory: %w", err)
	}

	ctx := context.Background()

	absProjectRoot, err := filepath.Abs(".")
	if err != nil {
		return nil, fmt.Errorf("could not determine absolute path: %w", err)
	}

	dockerfilePath := filepath.Join(os.TempDir(), "jm-asc-build")
	os.MkdirAll(dockerfilePath, os.ModePerm)

	dockerfileContent := `
FROM node:18
RUN npm install -g assemblyscript
RUN apt-get update && apt-get install -y wabt
WORKDIR /src
ENTRYPOINT ["tail", "-f", "/dev/null"]
`

	err = os.WriteFile(filepath.Join(dockerfilePath, "Dockerfile"), []byte(dockerfileContent), fileModeUserRW)
	if err != nil {
		return nil, fmt.Errorf("failed to write Dockerfile: %w", err)
	}

	fromDockerfile := testcontainers.FromDockerfile{
		Context:    dockerfilePath,
		Dockerfile: "Dockerfile",
	}

	req := testcontainers.ContainerRequest{
		FromDockerfile: fromDockerfile,
		Entrypoint:     []string{"tail", "-f", "/dev/null"},
		WaitingFor:     wait.ForLog(""),
		HostConfigModifier: func(hc *container.HostConfig) {
			hc.Binds = append(hc.Binds, fmt.Sprintf("%s:/src", absProjectRoot))
		},
	}

	ctr, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: req,
		Started:          true,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to start container: %w", err)
	}

	return &DockerBuilder{Container: ctr, RuntimePath: runtimePath}, nil
}

// BuildAssemblyScript compiles an AssemblyScript source file to WASM.
//
// The compilation uses AssemblyScript compiler (asc) with optimized settings
// for game scripting. The compiler options ensure:
//   - Proper linking with host functions
//   - Optimized output for runtime performance
//   - Correct memory management
//   - Compatibility with wasm3 interpreter
//
// Compiler options:
//   - --path: Sets the base path for resolving imports
//   - --outFile: Output WASM file path
//   - --optimize: Enables balanced optimizations (size and speed)
//   - --runtime stub: Uses minimal runtime (wasm3 handles memory management)
//   - --noAssert: Removes assertions for release builds (smaller, faster)
//
// Parameters:
//   - mergedSourcePath: Path to the merged script (prelude + stdlib + user script)
//   - outputPath: Path where the compiled WASM will be written
//
// Returns an error if:
//   - The compiler cannot be executed
//   - Compilation fails (syntax errors, type errors, etc.)
//   - Output file cannot be written
func (b *DockerBuilder) BuildAssemblyScript(mergedSourcePath, outputPath string) error {
	ctx := context.Background()

	// AssemblyScript compiler command with optimization flags
	// These flags ensure proper compilation and linking:
	//   - --path: Base path for module resolution
	//   - --outFile: Output WASM binary path
	//   - --optimize: Enable size and speed optimizations (balanced)
	//   - --runtime stub: Use minimal runtime (wasm3 handles GC and memory management)
	//   - --noAssert: Remove assertions for release builds (smaller, faster)
	//
	// Runtime selection:
	//   - "stub": Minimal runtime, never frees (bump allocation only)
	//   - Perfect for wasm3 which manages memory externally
	//   - Results in smallest binary size
	cmd := []string{
		"asc", mergedSourcePath,
		"--path", "/src/"+b.RuntimePath,
		"--outFile", outputPath,
		"--optimize",        // Enable optimizations (balanced size/speed)
		"--runtime", "stub", // Minimal runtime - wasm3 provides memory management
		"--noAssert",        // Remove assertions for release builds
	}

	exitCode, reader, err := b.Container.Exec(ctx, cmd)
	if err != nil {
		return fmt.Errorf("error executing asc: %w", err)
	}
	if exitCode != 0 {
		// Capture compiler error output for debugging
		buf := new(strings.Builder)
		if _, copyErr := io.Copy(buf, reader); copyErr != nil {
			return fmt.Errorf("asc failed but couldn't read output: %w", copyErr)
		}
		return fmt.Errorf("asc exited with code %d: %s", exitCode, buf.String())
	}

	return nil
}

// BuildScriptMetaData extracts import and export information from a compiled WASM module.
//
// This function uses wasm-objdump to analyze the WASM binary and extract:
//   - Imports: All host functions the script requires (from @external declarations)
//   - Exports: All functions the script exports (currently not extracted, returns empty)
//
// The import list is used to:
//   - Validate that all required host functions are registered in the engine
//   - Store in script metadata for runtime linking verification
//   - Provide better error messages if linking fails
//
// The wasm-objdump output format:
//   - Import section shows: <env.functionName>
//   - The regex extracts the function name from this format
//
// Parameters:
//   - outputPath: Path to the compiled WASM file
//
// Returns:
//   - ScriptMetaData with imports and exports (exports currently empty)
//   - Error if wasm-objdump fails or output cannot be parsed
func (b *DockerBuilder) BuildScriptMetaData(outputPath string) (stdlib.ScriptMetaData, error) {
	ctx := context.Background()

	// Use wasm-objdump to extract import/export information
	// -x flag shows detailed information including imports and exports
	cmd := []string{
		"wasm-objdump",
		"-x", // Detailed output including imports/exports
		outputPath,
	}

	exitCode, reader, err := b.Container.Exec(ctx, cmd)
	if err != nil {
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, fmt.Errorf("error executing wasm-objdump: %w", err)
	}
	if exitCode != 0 {
		buf := new(strings.Builder)
		if _, copyErr := io.Copy(buf, reader); copyErr != nil {
			return stdlib.ScriptMetaData{
				Imports: []string{},
				Exposed: []string{},
			}, fmt.Errorf("wasm-objdump failed but couldn't read output: %w", copyErr)
		}
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, fmt.Errorf("wasm-objdump exited with code %d: %s", exitCode, buf.String())
	}

	// Read and parse the objdump output
	buf := new(strings.Builder)
	if _, copyErr := io.Copy(buf, reader); copyErr != nil {
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, fmt.Errorf("wasm-objdump: couldn't read output: %w", copyErr)
	}

	// Extract imports from objdump output
	// Format: <env.functionName> or similar
	// The regex matches: <env.FUNCTION_NAME>
	imports := []string{}
	scanner := bufio.NewScanner(strings.NewReader(buf.String()))
	for scanner.Scan() {
		line := scanner.Text()
		matches := importRegex.FindStringSubmatch(line)
		if len(matches) == 2 {
			imports = append(imports, matches[1])
		}
	}

	if err := scanner.Err(); err != nil {
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, fmt.Errorf("error scanning wasm-objdump output: %w", err)
	}

	// Remove duplicates and sort for consistent output
	importSet := make(map[string]bool)
	uniqueImports := []string{}
	for _, imp := range imports {
		if !importSet[imp] {
			importSet[imp] = true
			uniqueImports = append(uniqueImports, imp)
		}
	}
	sort.Strings(uniqueImports)

	// TODO: Extract exports from objdump output
	// Currently exports are not extracted, but the structure is ready

	return stdlib.ScriptMetaData{
		Imports: uniqueImports,
		Exposed: []string{}, // Exports extraction not yet implemented
	}, nil
}

func (b *DockerBuilder) Close() {
	ctx := context.Background()
	b.Container.Terminate(ctx)
}
