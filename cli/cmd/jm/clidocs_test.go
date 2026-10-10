package main

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

// docs/cli.md is jm's help: run `go generate ./cmd/jm` after changing a command.
func TestCLIReferenceMatchesTheRepo(t *testing.T) {
	want, err := os.ReadFile("../../../docs/cli.md")
	if err != nil {
		t.Fatal(err)
	}
	got := cliReference(newRootCmd())
	if got != string(want) {
		t.Error("docs/cli.md is stale: run go generate ./cmd/jm")
	}
	for _, c := range []string{"## jm build", "## jm plays drive", "--json"} {
		if !strings.Contains(got, c) {
			t.Errorf("reference lacks %q", c)
		}
	}
}

// The guides jm ships (jm docs, AGENTS.md) name only commands this jm has.
func TestShippedDocsNameRealCommands(t *testing.T) {
	files, _ := filepath.Glob("../../internal/docs/md/*.md")
	// jm as a command: in backticks, at a line start, after a pipe/&&/; or an env prefix.
	command := regexp.MustCompile("(?m)(?:^[ \t]*|`|[|;&][ \t]*|=\\S*[ \t]+)jm ([a-z][\\w-]*)")
	root := newRootCmd()
	for _, f := range files {
		text, _ := os.ReadFile(f)
		for _, m := range command.FindAllStringSubmatch(string(text), -1) {
			if c, _, err := root.Find([]string{m[1]}); err != nil || c == root {
				t.Errorf("%s names jm %s, which isn't a command", filepath.Base(f), m[1])
			}
		}
	}
	if len(files) == 0 {
		t.Fatal("found no shipped docs")
	}
}
