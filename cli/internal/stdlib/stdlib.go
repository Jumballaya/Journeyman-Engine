// Package stdlib exposes the embedded `@jm/runtime` AssemblyScript package.
// The CLI extracts these files into a project's `assets/scripts/node_modules/`
// at build time so user scripts can `import { ... } from "@jm/runtime"`.
package stdlib

import "embed"

// StdLibFiles holds the @jm/runtime source files (TypeScript + package.json)
// embedded at compile time. Use SyncRuntime / extraction helpers to materialize
// them into a project's node_modules.
//
//go:embed runtime/*.ts runtime/package.json
var StdLibFiles embed.FS

// ScriptMetaData carries per-script wasm-introspected metadata that pack mode
// writes into archive resolver entries. Populated by the wasm parser at build
// time. Folder-mode runtime ignores both fields.
type ScriptMetaData struct {
	Imports []string
	Exposed []string
}
