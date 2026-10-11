// Package stdlib exposes the embedded `@jm/runtime` AssemblyScript package and
// the stock `@jm/behaviors` scripts. The CLI extracts both into a project's
// `assets/scripts/node_modules/` at build time so user scripts can
// `import { ... } from "@jm/runtime"` and content can attach a behavior.
package stdlib

import "embed"

// StdLibFiles holds the @jm/runtime sources; jm build extracts them.
//
//go:embed runtime/*.ts runtime/package.json behaviors/*.ts behaviors/package.json
var StdLibFiles embed.FS
