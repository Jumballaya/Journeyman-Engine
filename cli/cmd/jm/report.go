package main

import (
	"encoding/json"
	"fmt"
	"os"
	"regexp"
	"strconv"
	"strings"
)

// Diagnostic is one problem a command found. With --json, each is a line of
// JSON on stdout and the last line is the result:
//
//	{"level":"error","category":"script","message":"...","file":"assets/scripts/hero.ts","line":12,"column":5}
//	{"result":"failed","errors":1,"warnings":0}
type Diagnostic struct {
	Level    string `json:"level"`              // error or warning
	Category string `json:"category,omitempty"` // script, content, atlas, tileset, manifest, toolchain, build
	Message  string `json:"message"`
	File     string `json:"file,omitempty"`
	Line     int    `json:"line,omitempty"`
	Column   int    `json:"column,omitempty"`
}

// jsonOutput: progress is silent and problems are JSON lines (set by --json).
var jsonOutput bool

var errorCount, warningCount int

// say prints progress, which --json leaves out.
func say(format string, args ...any) {
	if !jsonOutput {
		fmt.Printf(format+"\n", args...)
	}
}

func emit(d Diagnostic) {
	switch d.Level {
	case "warning":
		warningCount++
	case "error":
		errorCount++
	}
	if jsonOutput {
		line, _ := json.Marshal(d)
		fmt.Println(string(line))
		return
	}
	where := d.File
	if where != "" && d.Line > 0 {
		where += fmt.Sprintf(":%d:%d", d.Line, d.Column)
	}
	if where != "" {
		where += ": "
	}
	prefix := ""
	if d.Level == "warning" {
		prefix = "warning: "
	}
	fmt.Println(prefix + where + d.Message)
}

// fail reports d and ends the command with exit code 1.
func fail(d Diagnostic) {
	d.Level = "error"
	emit(d)
	finish(false)
}

// finish ends a --json run with its result line; failed runs exit 1.
func finish(ok bool) {
	if jsonOutput {
		result := map[string]any{"result": "ok", "errors": errorCount, "warnings": warningCount}
		if !ok {
			result["result"] = "failed"
		}
		line, _ := json.Marshal(result)
		fmt.Println(string(line))
	}
	if !ok {
		os.Exit(1)
	}
}

// ascDiagnostic matches an AssemblyScript compiler message's first line, and
// ascLocation the "in file(line,column)" that follows it.
var (
	ascDiagnostic = regexp.MustCompile(`^(ERROR|WARNING) (\w+): (.*)$`)
	ascLocation   = regexp.MustCompile(`in (\S+?)\((\d+),(\d+)\)`)
)

// parseAsc turns asc's output into diagnostics; `resolve` maps the paths it
// prints (relative to the scripts package) to project paths.
func parseAsc(output string, resolve func(string) string) []Diagnostic {
	var out []Diagnostic
	for _, line := range strings.Split(output, "\n") {
		line = strings.TrimSpace(line)
		if m := ascDiagnostic.FindStringSubmatch(line); m != nil {
			out = append(out, Diagnostic{Level: strings.ToLower(m[1]), Category: "script", Message: m[2] + ": " + m[3]})
			continue
		}
		if m := ascLocation.FindStringSubmatch(line); m != nil && len(out) > 0 && out[len(out)-1].File == "" {
			last := &out[len(out)-1]
			last.File = resolve(m[1])
			last.Line, _ = strconv.Atoi(m[2])
			last.Column, _ = strconv.Atoi(m[3])
		}
	}
	return out
}
