package main

import (
	"os"
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
