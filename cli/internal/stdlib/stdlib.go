// Package stdlib exposes the embedded `@jm/runtime` AssemblyScript package.
// The CLI extracts these files into a project's `assets/scripts/node_modules/`
// at build time so user scripts can `import { ... } from "@jm/runtime"`.
package stdlib

import "embed"

// StdLibFiles holds the @jm/runtime sources; jm build extracts them.
//
//go:embed runtime/*.ts runtime/package.json
var StdLibFiles embed.FS
