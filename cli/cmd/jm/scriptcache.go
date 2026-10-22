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
	workers := 1 // a config's own output file (textFile) is every compile's
	if cache.plain {
		workers = min(runtime.NumCPU(), 8, len(compiles))
	}
	for range workers {
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
// what compiling may read (dir "": none kept). plain: its config is
// (ascConfigs), so compiles read and write only their own files and may run
// at once; else one at a time, none kept.
type scriptCache struct {
	dir   string
	plain bool
}

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
		return scriptCache{plain: false}
	}
	for file, text := range configs {
		read["asconfig "+file] = text
	}
	top, err := jmfolder.Make(root, scriptCacheDir)
	if err != nil {
		return scriptCache{plain: true}
	}
	key := sourcesPrint(read)
	if kept, err := os.ReadDir(top); err == nil {
		for _, k := range kept {
			if k.Name() != key {
				os.RemoveAll(filepath.Join(top, k.Name()))
			}
		}
	}
	return scriptCache{filepath.Join(top, key), true}
}

// ascConfigs are the asconfig at file and each it extends, by their paths;
// false when a compile may read or write more than the cache knows: a
// config from a package, or one that isn't plain.
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
		if !plainConfig(config) {
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

// codegenOptions are asc's options that shape the one wasm a compile writes
// (and its map), and read nothing but the script and its imports.
var codegenOptions = map[string]bool{"exportRuntime": true, "noAssert": true, "optimize": true, "optimizeLevel": true,
	"shrinkLevel": true, "converge": true, "debug": true, "runtime": true, "initialMemory": true, "maximumMemory": true,
	"memoryBase": true, "tableBase": true, "noExportMemory": true, "importMemory": true, "sharedMemory": true,
	"zeroFilledMemory": true, "importTable": true, "exportTable": true, "exportStart": true, "lowMemoryLimit": true,
	"enable": true, "disable": true, "noUnsafe": true, "pedantic": true, "stackSize": true, "uncheckedBehavior": true,
	"trapMode": true, "noValidate": true, "use": true, "sourceMap": true}

// plainConfig says whether a config has asc read and write no more than a
// script, its imports, its wasm and its map: only codegen options (its own,
// or a target's) naming no file.
func plainConfig(config map[string]any) bool {
	sets := []any{config["options"]}
	for key, v := range config {
		switch key {
		case "targets":
			targets, _ := v.(map[string]any)
			for _, t := range targets {
				sets = append(sets, t)
			}
		case "options", "extends", "$schema":
		default:
			return false // entries, …: more to read
		}
	}
	for _, set := range sets {
		options, ok := set.(map[string]any)
		if set != nil && !ok {
			return false
		}
		for key, v := range options {
			if !codegenOptions[key] || !plainOption(key, v) {
				return false
			}
		}
	}
	return true
}

// plainOption says whether an option's value names no file: a source map by
// its default name, a runtime of asc's own.
func plainOption(key string, v any) bool {
	switch key {
	case "sourceMap":
		_, named := v.(string)
		return !named
	case "runtime":
		return v == "incremental" || v == "minimal" || v == "stub"
	}
	return true
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
