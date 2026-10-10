package main

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// watchSources rebuilds the project at root whenever its source files change
// (settled for a moment: an editor saving several at once), until ctx ends.
// The engine, run with JM_WATCH=1, then reloads what the build changed.
func watchSources(ctx context.Context, root string) {
	self, err := executablePath()
	if err != nil {
		return
	}
	last := sourceTimes(root)
	for {
		select {
		case <-ctx.Done():
			return
		case <-time.After(500 * time.Millisecond):
		}
		now := sourceTimes(root)
		if sameTimes(now, last) {
			continue
		}
		time.Sleep(300 * time.Millisecond) // let a burst of saves finish
		last = sourceTimes(root)
		build := exec.CommandContext(ctx, self, "build")
		build.Dir = root
		build.Stdout, build.Stderr = os.Stderr, os.Stderr // stdout is the game's (the driver's answers)
		_ = build.Run()                                   // its errors are printed; the game keeps the last good build
	}
}

// sourceTimes are the modification times of what jm build reads: the
// manifest, its scenes, the files its asset entries match, and everything in
// assets/ (atlas source images too). Not what a run writes (logs, captures,
// saves), so a run never rebuilds itself.
func sourceTimes(root string) map[string]time.Time {
	times := map[string]time.Time{}
	stamp := func(rel string) {
		if info, err := os.Stat(filepath.Join(root, rel)); err == nil {
			times[rel] = info.ModTime()
		}
	}
	stamp(archive.ManifestEntryKey)
	man, err := manifest.LoadManifest(filepath.Join(root, archive.ManifestEntryKey))
	if err != nil {
		return times
	}
	files, _ := manifest.ExpandAssets(os.DirFS(root), append([]string{"assets/**"}, man.Assets...))
	for _, rel := range append(files, man.Scenes...) {
		stamp(rel)
	}
	return times
}

func sameTimes(a, b map[string]time.Time) bool {
	if len(a) != len(b) {
		return false
	}
	for path, t := range a {
		if !b[path].Equal(t) {
			return false
		}
	}
	return true
}
