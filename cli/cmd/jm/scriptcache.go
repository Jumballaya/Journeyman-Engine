package main

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/jmfolder"
	"github.com/Jumballaya/Journeyman-Engine/internal/toolchain"
)

// A build compiles its scripts at once, and keeps what compiles cleanly by
// what compiling reads: the game's and its libraries' scripts and script
// package, this jm and its toolchain. One changed compiles them all again;
// none changed (a scene edited) compiles none.

// A compile is one script's: shipped in the build, or a module only checked.
type compile struct {
	script, out string // project-relative source; where its wasm goes
	module      bool
}

// compileScripts compiles each, then reports them in order: asc's messages,
// and each that doesn't compile.
func compileScripts(r *report, tc toolchain.Toolchain, projectRoot string, cache scriptCache, compiles []compile) {
	type result struct {
		output string
		err    error
	}
	results := make([]result, len(compiles))
	jobs := make(chan int)
	var wg sync.WaitGroup
	for range min(runtime.NumCPU(), 8, len(compiles)) {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := range jobs {
				c := compiles[i]
				if cache.restore(c) {
					continue
				}
				output, err := compileScript(tc, c.script, projectRoot, c.out)
				if err == nil && strings.TrimSpace(output) == "" {
					cache.keep(c)
				}
				results[i] = result{output, err}
			}
		}()
	}
	for i := range compiles {
		jobs <- i
	}
	close(jobs)
	wg.Wait()
	for i, c := range compiles {
		reportAsc(r, projectRoot, results[i].output)
		switch {
		case results[i].err != nil:
			r.add(Diagnostic{Level: "error", Category: "script", File: c.script, Message: "doesn't compile (asc: " + results[i].err.Error() + ")"})
		case c.module:
			say("Checked module: %s (no scene or prefab attaches it)", c.script)
		default:
			say("Built script: %s", c.script)
		}
	}
}

// scriptCache is where a game's compiled scripts are kept, by the print of
// what compiling reads (dir ""; none kept).
type scriptCache struct{ dir string }

const scriptCacheDir = "cache/scripts" // in the game's .jm/

// openScriptCache is the game at root's cache for its sources now, the
// others it kept dropped (their scripts changed since).
func openScriptCache(root string, sources map[string]string, tc toolchain.Toolchain) scriptCache {
	read := map[string]string{"jm/entry": entryTemplate, "jm/toolchain": tc.Node + "\x00" + tc.ASC}
	for key, stamp := range sources {
		if filepath.IsAbs(key) || strings.HasSuffix(key, ".ts") || strings.HasPrefix(key, scriptsPkgDir+"/") { // a library's files are by their paths
			read[key] = stamp
		}
	}
	top, err := jmfolder.Make(root, scriptCacheDir)
	if err != nil {
		return scriptCache{}
	}
	key := sourcesPrint(read)
	if kept, err := os.ReadDir(top); err == nil {
		for _, k := range kept {
			if k.Name() != key {
				os.RemoveAll(filepath.Join(top, k.Name()))
			}
		}
	}
	return scriptCache{filepath.Join(top, key)}
}

func (c scriptCache) at(script string) string {
	return filepath.Join(c.dir, filepath.FromSlash(script))
}

// restore puts a kept compile's wasm where it goes; false when none is kept.
func (c scriptCache) restore(job compile) bool {
	return c.dir != "" && copyFile(c.at(job.script), job.out) == nil
}

// keep keeps a compile's wasm, whole or not at all (best effort: a build
// that can't keep it still built).
func (c scriptCache) keep(job compile) {
	data, err := os.ReadFile(job.out)
	if c.dir == "" || err != nil || os.MkdirAll(filepath.Dir(c.at(job.script)), 0o755) != nil {
		return
	}
	_ = atomicfile.WriteFile(c.at(job.script), data, 0o644)
}
