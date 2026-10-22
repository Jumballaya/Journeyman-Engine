package main

import (
	"context"
	"maps"
	"os"
	"os/exec"
	"time"
)

// watchSources rebuilds the project at root whenever its source files change
// (settled for a moment: an editor saving several at once), until ctx ends.
// The engine, run with JM_WATCH=1, then reloads what the build changed.
func watchSources(ctx context.Context, root string) {
	self, err := executablePath()
	if err != nil {
		return
	}
	last := buildSources(root)
	for {
		select {
		case <-ctx.Done():
			return
		case <-time.After(500 * time.Millisecond):
		}
		now := buildSources(root)
		if maps.Equal(now, last) {
			continue
		}
		time.Sleep(300 * time.Millisecond) // let a burst of saves finish
		last = buildSources(root)
		build := exec.CommandContext(ctx, self, "build")
		build.Dir = root
		build.Stdout, build.Stderr = os.Stderr, os.Stderr // stdout is the game's (the driver's answers)
		_ = build.Run()                                   // its errors are printed; the game keeps the last good build
	}
}
