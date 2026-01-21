// Package stdlib provides the standard library for AssemblyScript game scripts.
//
// The stdlib consists of:
//   - Prelude: Host function declarations (@external) that match engine functions
//   - Runtime libraries: High-level APIs (console, audio, ECS, time)
//
// During compilation, the stdlib is prepended to user scripts, providing:
//   1. Type-safe access to engine host functions
//   2. Convenient high-level APIs for common operations
//   3. Consistent runtime behavior across all scripts
//
// The compilation process:
//   1. Load prelude.ts (host function declarations)
//   2. Load all runtime/*.ts files (APIs)
//   3. Merge: prelude + runtime libs + user script
//   4. Compile merged script with AssemblyScript compiler (asc)
//   5. Extract imports from compiled WASM for linking validation
//
// Linking:
//   - Host functions are linked at runtime by wasm3
//   - Function signatures must match exactly between prelude and engine
//   - Missing or mismatched functions cause runtime linking errors
package stdlib

import (
	"embed"
	"fmt"
	"io/fs"
	"path/filepath"
	"sort"
	"strings"
)

//go:embed runtime/*.ts
var StdLibFiles embed.FS

// StdLib contains the preprocessed standard library content.
// The prelude and lib content are kept separate for clarity and potential
// future optimizations (e.g., conditional inclusion).
type StdLib struct {
	// PreludeContent contains the host function declarations (prelude.ts).
	// This must be included first so host functions are available to runtime libs.
	PreludeContent string

	// LibContent contains all runtime library files concatenated together.
	// Files are loaded in alphabetical order for deterministic output.
	LibContent string
}

// ScriptMetaData contains information extracted from a compiled WASM module.
// This is used to validate that all required host functions are available
// and to track what the script exports.
type ScriptMetaData struct {
	// Imports lists all host functions the script requires (from @external declarations).
	// These must be registered in the engine's ScriptManager before the script can run.
	Imports []string

	// Exposed lists all functions the script exports (for potential future use).
	// Currently used for tracking but not actively validated.
	Exposed []string
}

// CreateStdLib loads and prepares the standard library for script compilation.
//
// The stdlib is embedded at compile time, so this function always succeeds
// unless there's a programming error (missing embedded files).
//
// Returns an error if:
//   - prelude.ts cannot be read (should never happen)
//   - Runtime library files cannot be read (should never happen)
//
// Example usage:
//   stdlib, err := CreateStdLib()
//   if err != nil {
//       return fmt.Errorf("failed to load stdlib: %w", err)
//   }
//   mergedScript := stdlib.BuildScript(userScript)
func CreateStdLib() (*StdLib, error) {
	// Load prelude (host function declarations)
	// This must be first so @external declarations are available
	preludeData, err := StdLibFiles.ReadFile("runtime/prelude.ts")
	if err != nil {
		return nil, fmt.Errorf("failed to load prelude.ts: %w", err)
	}

	// Build library content (runtime APIs)
	// Files are concatenated in alphabetical order for deterministic output
	libContent, err := buildLibContent()
	if err != nil {
		return nil, fmt.Errorf("failed to build lib content: %w", err)
	}

	return &StdLib{
		PreludeContent: string(preludeData),
		LibContent:     libContent,
	}, nil
}

// buildLibContent loads all runtime library files and concatenates them.
//
// Files are processed in alphabetical order to ensure deterministic output.
// The prelude.ts file is explicitly excluded (it's handled separately).
//
// Returns the concatenated content with double newlines between files.
func buildLibContent() (string, error) {
	entries, err := fs.ReadDir(StdLibFiles, "runtime")
	if err != nil {
		return "", fmt.Errorf("failed to list embedded stdlib files: %w", err)
	}

	// Sort entries by name for deterministic ordering
	fileNames := make([]string, 0, len(entries))
	for _, entry := range entries {
		if !entry.IsDir() && entry.Name() != "prelude.ts" {
			fileNames = append(fileNames, entry.Name())
		}
	}
	sort.Strings(fileNames)

	var builder strings.Builder

	// Load and concatenate files in sorted order
	for _, fileName := range fileNames {
		data, err := StdLibFiles.ReadFile(filepath.Join("runtime", fileName))
		if err != nil {
			return "", fmt.Errorf("failed to read embedded stdlib file %s: %w", fileName, err)
		}

		builder.WriteString(string(data))
		builder.WriteString("\n\n") // Double newline separates files
	}

	return builder.String(), nil
}

// BuildScript merges the standard library with a user script.
//
// The merge order is critical:
//   1. Prelude (host function declarations) - must be first
//   2. Runtime libraries (APIs that use host functions)
//   3. User script (can use both prelude and runtime APIs)
//
// This order ensures:
//   - Host functions are declared before use
//   - Runtime APIs can use host functions
//   - User scripts can use both host functions and runtime APIs
//
// Parameters:
//   - userScript: The user's TypeScript/AssemblyScript code
//
// Returns:
//   - Complete script ready for AssemblyScript compilation
//
// Example:
//   merged := stdlib.BuildScript(`
//     export function onUpdate(dt: f32): void {
//       console.log("Delta time: ", dt.toString());
//     }
//   `)
func (s *StdLib) BuildScript(userScript string) string {
	var builder strings.Builder

	// 1. Prelude: Host function declarations (@external)
	//    These must come first so they're available to everything else
	builder.WriteString(s.PreludeContent)
	builder.WriteString("\n\n")

	// 2. Runtime libraries: High-level APIs
	//    These use the host functions declared in prelude
	builder.WriteString(s.LibContent)
	builder.WriteString("\n\n")

	// 3. User script: Game logic
	//    Can use both host functions and runtime APIs
	builder.WriteString(userScript)

	return builder.String()
}

// BuildScriptImports extracts import requirements from wasm-objdump output.
//
// This function is currently a stub. The actual import extraction is done
// in the docker package using wasm-objdump, which parses the WASM binary
// to find all imported functions.
//
// Parameters:
//   - objDump: Output from `wasm-objdump -x <wasm-file>`
//
// Returns:
//   - List of imported function names (e.g., ["__jmLog", "__jmPlaySound"])
//
// Note: This is kept for potential future use or alternative implementations.
// The docker package currently handles import extraction directly.
func (s *StdLib) BuildScriptImports(objDump string) []string {
	// TODO: Implement import extraction from objdump output if needed
	// Currently handled in docker.BuildScriptMetaData()
	list := make([]string, 0)
	return list
}
