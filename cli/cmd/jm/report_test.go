package main

import (
	"errors"
	"fmt"
	"io"
	"os"
	"strings"
	"testing"
)

// printed is what f prints on stdout.
func printed(t *testing.T, f func()) string {
	t.Helper()
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	stdout := os.Stdout
	os.Stdout = w
	defer func() { os.Stdout = stdout }()
	f()
	w.Close()
	out, _ := io.ReadAll(r)
	return string(out)
}

func TestAReportsOutcomeIsWhatItFound(t *testing.T) {
	old := jsonOutput
	jsonOutput = true
	defer func() { jsonOutput = old }()

	var err error
	out := printed(t, func() {
		r := &report{category: "build"}
		r.add(Diagnostic{Level: "warning", Category: "art", Message: "off palette"})
		err = r.end(fmt.Errorf("Failed to copy a.png: %w", errors.New("gone")))
	})
	var exit exitCode
	if !errors.As(err, &exit) || exit.code != 1 || exit.msg != "" {
		t.Fatalf("a stopped run ends with exit 1, said already: %#v", err)
	}
	var problems []Diagnostic
	var result map[string]any
	for _, line := range strings.Split(out, "\n") {
		if d, res := readReportLine(line); d != nil {
			problems = append(problems, *d)
		} else if res != nil {
			result = res
		}
	}
	if len(problems) != 2 || problems[1] != (Diagnostic{Level: "error", Category: "build", Message: "Failed to copy a.png: gone"}) {
		t.Errorf("the stray error is the report's category's problem: %+v", problems)
	}
	if result["result"] != "failed" || result["errors"] != 1.0 || result["warnings"] != 1.0 {
		t.Errorf("result line = %v", result)
	}

	out = printed(t, func() {
		r := &report{category: "golden"}
		err = r.end(Diagnostic{Category: "toolchain", Message: "no node"})
	})
	if d, _ := readReportLine(strings.Split(out, "\n")[0]); d == nil || d.Category != "toolchain" || d.Level != "error" {
		t.Errorf("a Diagnostic returned keeps its category: %q", out)
	}

	out = printed(t, func() { err = (&report{}).end(nil) })
	if err != nil || !strings.Contains(out, `"result":"ok"`) {
		t.Errorf("nothing found: ok, %v %q", err, out)
	}
}

func TestReadReportLineTellsProblemsFromResults(t *testing.T) {
	for _, c := range []struct {
		line            string
		problem, result bool
	}{
		{`{"level":"error","category":"script","message":"x","file":"a.ts","line":1,"column":2}`, true, false},
		{`{"level":"error","category":"test","test":"jumps","message":"x"}`, true, false},
		{`{"test":"jumps","result":"pass"}`, false, false}, // one test's, not the run's
		{`{"result":"ok","passed":3,"failed":0}`, false, true},
		{`{"result":"failed","errors":1,"warnings":0}`, false, true},
		{`{"level":"info","message":"x"}`, false, false},
		{`Building game...`, false, false},
		{`warning: off palette`, false, false},
	} {
		d, result := readReportLine(c.line)
		if (d != nil) != c.problem || (result != nil) != c.result {
			t.Errorf("readReportLine(%s) = %v, %v", c.line, d, result)
		}
	}
}
