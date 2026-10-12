// Package templates holds starting files that jm and the editor both write;
// the editor compiles these same files in.
package templates

import _ "embed"

// Script is a new script: jm generate script, jm init's player, the editor's New Script.
//
//go:embed script.ts
var Script string
