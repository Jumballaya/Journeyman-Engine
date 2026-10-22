// Package gametree says which of a game's folders are the game's own: not
// what jm, its runs and tools make there, nor dot-folders (.git, .jm).
package gametree

import (
	"slices"
	"strings"
)

// Made are the folders jm and runs make at a game's top: builds and their
// staging, exports, engine logs, captured frames. Deeper, they're the game's
// (assets/frames: a sprite's).
var Made = []string{"build", "build.next", "build.next.modules", "build.old", "dist", "logs", "frames"}

// Skip says whether the folder at slash path dir (from the game's folder) is
// none of the game's: one Made, or in one; or a dot-folder (.git, .jm) or npm
// install anywhere. Case-blind: macOS's and Windows' disks don't tell BUILD from build.
func Skip(dir string) bool {
	for i, part := range strings.Split(dir, "/") {
		if part == "." || part == "" {
			continue
		}
		if strings.HasPrefix(part, ".") || strings.EqualFold(part, "node_modules") ||
			i == 0 && slices.ContainsFunc(Made, func(m string) bool { return strings.EqualFold(m, part) }) {
			return true
		}
	}
	return false
}
