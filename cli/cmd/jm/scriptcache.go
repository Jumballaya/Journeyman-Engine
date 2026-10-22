package main

import (
	"encoding/json"
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
	configs, known := ascConfigs(scriptsPath(root, "asconfig.json")) // a base it extends may be anywhere
	if !known {
		return scriptCache{}
	}
	for file, text := range configs {
		read["asconfig "+file] = text
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

// ascConfigs are the asconfig at file and each it extends, by their paths;
// false when what a compile reads or writes is more than the cache knows: a
// config from a package, or a file asc writes but the wasm and its map.
func ascConfigs(file string) (map[string]string, bool) {
	out := map[string]string{}
	for out[file] == "" {
		data, err := os.ReadFile(file)
		if err != nil {
			out[file] = "none"
			return out, true
		}
		out[file] = string(data)
		var config map[string]any
		if json.Unmarshal(data, &config) != nil {
			return out, true // asc says why
		}
		if writesMore(config) {
			return nil, false
		}
		next, _ := config["extends"].(string)
		switch {
		case next == "":
			return out, true
		case filepath.IsAbs(next):
			file = next
		case strings.HasPrefix(next, "./") || strings.HasPrefix(next, "../"):
			file = filepath.Join(filepath.Dir(file), next)
		default:
			return nil, false // a package's: resolved as node does
		}
	}
	return out, true // a loop: asc's to report
}

// writesMore says whether a config (its options, or a target's) has asc
// write a file but the wasm and its out.wasm.map.
func writesMore(config map[string]any) bool {
	sets := []any{config["options"]}
	if targets, ok := config["targets"].(map[string]any); ok {
		for _, t := range targets {
			sets = append(sets, t)
		}
	}
	for _, set := range sets {
		options, _ := set.(map[string]any)
		for _, key := range []string{"textFile", "jsFile", "tsdFile", "idlFile", "bindings", "outFile"} {
			if options[key] != nil && options[key] != false {
				return true
			}
		}
		if named, ok := options["sourceMap"].(string); ok && named != "" {
			return true
		}
	}
	return false
}

// sourceMap is where asc writes a source map for a compile to out: its name
// without the .ts, .wasm.map.
func sourceMap(out string) string { return strings.TrimSuffix(out, filepath.Ext(out)) + ".wasm.map" }

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
		return copyFile(filepath.Join(dir, "wasm.map"), sourceMap(job.out)) == nil
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
	if exists(sourceMap(job.out)) && !keepFile(sourceMap(job.out), filepath.Join(dir, "wasm.map")) {
		return
	}
	keepFile(job.out, filepath.Join(dir, "wasm"))
}

func keepFile(from, to string) bool {
	data, err := os.ReadFile(from)
	return err == nil && atomicfile.WriteFile(to, data, 0o644) == nil
}
