package main

import (
	"encoding/json"
	"errors"
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
	Category string `json:"category,omitempty"` // script, content, atlas, tileset, manifest, toolchain, build, editor
	Message  string `json:"message"`
	File     string `json:"file,omitempty"`
	Line     int    `json:"line,omitempty"`
	Column   int    `json:"column,omitempty"`
}

// A Diagnostic is an error too: a command returns the one that stops it.
func (d Diagnostic) Error() string { return d.Message }

// jsonOutput: progress is silent and problems are JSON lines (set by --json).
var jsonOutput bool

// say prints progress, which --json leaves out.
func say(format string, args ...any) {
	if !jsonOutput {
		fmt.Printf(format+"\n", args...)
	}
}

// stage tells the jm mcp tool call running this jm (JM_PROGRESS) what it's doing, e.g. "Building".
func stage(format string, args ...any) {
	if os.Getenv("JM_PROGRESS") != "" {
		fmt.Fprintf(os.Stderr, progressPrefix+format+"\n", args...)
	}
}

// A report is what a checking command (build, golden) finds: its problems as
// it goes, then its outcome (end). Category is a stray error's.
type report struct {
	category         string
	errors, warnings int
}

// add prints d: a line of JSON with --json, else as a compiler would.
func (r *report) add(d Diagnostic) {
	switch d.Level {
	case "warning":
		r.warnings++
	case "error":
		r.errors++
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

// end is the command's outcome: failed (exit 1, said already) when err stopped
// it or it found an error. err is reported first; --json's result line last.
func (r *report) end(err error) error {
	if err != nil {
		var d Diagnostic
		if !errors.As(err, &d) {
			d = Diagnostic{Category: r.category, Message: err.Error()}
		}
		d.Level = "error"
		r.add(d)
	}
	ok := r.errors == 0
	if jsonOutput {
		result := map[string]any{"result": "ok", "errors": r.errors, "warnings": r.warnings}
		if !ok {
			result["result"] = "failed"
		}
		line, _ := json.Marshal(result)
		fmt.Println(string(line))
	}
	if !ok {
		return exitCode{code: 1}
	}
	return nil
}

// readReportLine is a line of a --json run as add and end (or jm test's
// reporter) print it: a problem, or (result non-nil) the run's result line;
// neither for other output, a passing test's line among it.
func readReportLine(line string) (problem *Diagnostic, result map[string]any) {
	line = strings.TrimSpace(line)
	var m map[string]any
	if !strings.HasPrefix(line, "{") || json.Unmarshal([]byte(line), &m) != nil {
		return nil, nil
	}
	if level := m["level"]; level == "error" || level == "warning" {
		var d Diagnostic
		if json.Unmarshal([]byte(line), &d) == nil {
			return &d, nil
		}
	}
	if m["result"] != nil && m["test"] == nil {
		return nil, m
	}
	return nil, nil
}

// exitCode ends jm with code, printing msg first ("": said already, or nothing to say).
type exitCode struct {
	code int
	msg  string
}

func (e exitCode) Error() string { return e.msg }

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
