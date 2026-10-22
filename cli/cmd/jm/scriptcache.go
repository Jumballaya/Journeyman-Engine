package main

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/formats"
	"github.com/Jumballaya/Journeyman-Engine/internal/jmfolder"
	"github.com/Jumballaya/Journeyman-Engine/internal/toolchain"
)

// A build compiles its scripts at once, and keeps what compiles cleanly by
// what compiling may read: every source but what no compiler reads (scenes,
// art, sounds: formats' Compiled), this jm and its compiler. One changed
// compiles them all again; none (a scene edited) compiles none.

// A compile is one script's: shipped in the build, or a module only checked.
type compile struct {
	script, out string // project-relative source; where its wasm goes (asc writes out.* beside it too)
	module      bool
}

// ascSaid is what asc printed, on its streams.
type ascSaid struct{ stdout, stderr string }

func (s ascSaid) clean() bool { return strings.TrimSpace(s.stdout+s.stderr) == "" }

// compileScripts compiles each, then reports them in order: asc's messages,
// and each that doesn't compile.
func compileScripts(r *report, tc toolchain.Toolchain, projectRoot string, cache scriptCache, compiles []compile) {
	type result struct {
		said ascSaid
		err  error
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
				said, err := compileScript(tc, c.script, projectRoot, c.out)
				if err == nil && said.clean() {
					cache.keep(c)
				}
				results[i] = result{said, err}
			}
		}()
	}
	for i := range compiles {
		jobs <- i
	}
	close(jobs)
	wg.Wait()
	for i, c := range compiles {
		reportAsc(r, projectRoot, results[i].said)
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
// what compiling may read (dir "": none kept).
type scriptCache struct{ dir string }

const scriptCacheDir = "cache/scripts" // in the game's .jm/

// openScriptCache is the game at root's cache for its sources now, the
// others it kept dropped (their scripts changed since).
func openScriptCache(root string, sources map[string]string, tc toolchain.Toolchain) scriptCache {
	ascPackage, _ := os.ReadFile(filepath.Join(tc.ASC, "package.json")) // its version: a compiler replaced in place
	read := map[string]string{"jm/entry": entryTemplate,
		"jm/compiler": fmt.Sprint(tc.Node, "\x00", tc.NodeVer, "\x00", tc.ASC, "\x00", string(ascPackage))}
	for key, stamp := range sources {
		if f, known := formats.Of(key); !known || f.Compiled { // a library's files too, by their paths
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

// kept is the folder a script's compile is kept in: "wasm", and "wasm.map"
// when asc wrote a source map beside it (the wasm names it).
func (c scriptCache) kept(script string) string {
	return filepath.Join(c.dir, filepath.FromSlash(script))
}

// restore puts a kept compile's files where they go; false when none is kept.
func (c scriptCache) restore(job compile) bool {
	dir := c.kept(job.script)
	if c.dir == "" || copyFile(filepath.Join(dir, "wasm"), job.out) != nil {
		return false
	}
	if exists(filepath.Join(dir, "wasm.map")) {
		return copyFile(filepath.Join(dir, "wasm.map"), job.out+".map") == nil
	}
	return true
}

// keep keeps a compile's files, its wasm last: a folder without one is none
// kept. Best effort: a build that can't keep them still built.
func (c scriptCache) keep(job compile) {
	dir := c.kept(job.script)
	if c.dir == "" || os.RemoveAll(dir) != nil || os.MkdirAll(dir, 0o755) != nil {
		return
	}
	if exists(job.out+".map") && !keepFile(job.out+".map", filepath.Join(dir, "wasm.map")) {
		return
	}
	keepFile(job.out, filepath.Join(dir, "wasm"))
}

func keepFile(from, to string) bool {
	data, err := os.ReadFile(from)
	return err == nil && atomicfile.WriteFile(to, data, 0o644) == nil
}
