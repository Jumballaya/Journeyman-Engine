package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

//go:embed testrunner.mjs
var testRunner []byte

//go:embed testreporter.mjs
var testReporter []byte

var testCmd = &cobra.Command{
	Use:   "test [spec.ts ...]",
	Short: "Run the project's script tests (tests/*.spec.ts)",
	Long: `Compiles each spec with the project's AssemblyScript and runs every exported
function as a test; a failed assert fails it. GameState and Save are in-memory
and Data reads the project's files; other engine calls do nothing, so test
game logic (rules, data, state).
Needs no prior jm build.

--json prints a JSON line per test ({"test", "result": "pass"}, or an error
with "test", "message", "file", "line", "column") and a last line
{"result": "ok"|"failed", "passed", "failed"}.`,
	RunE: func(cmd *cobra.Command, args []string) error {
		projectRoot, err := os.Getwd()
		if err != nil {
			return err
		}
		return runTests(projectRoot, args)
	},
}

func runTests(projectRoot string, specs []string) error {
	tc, err := scriptToolchain(projectRoot)
	if err != nil {
		return err
	}
	m, err := manifest.LoadManifest(filepath.Join(projectRoot, archive.ManifestEntryKey))
	if err != nil {
		return fmt.Errorf("load manifest: %w", err)
	}
	if err := syncScriptPackages(projectRoot, m, tc); err != nil {
		return err
	}
	if len(specs) == 0 {
		specs, _ = filepath.Glob(filepath.Join(projectRoot, "tests", "*.spec.ts"))
	}
	if len(specs) == 0 {
		return fmt.Errorf("no tests: add tests/*.spec.ts (each exported function is a test)")
	}
	abs := make([]string, len(specs))
	for i, s := range specs {
		if abs[i], err = filepath.Abs(s); err != nil {
			return err
		}
	}
	list, _ := json.Marshal(abs)

	runner := scriptsPath(projectRoot, "node_modules", ".jm", "test-runner.mjs")
	if err := os.MkdirAll(filepath.Dir(runner), 0755); err != nil {
		return err
	}
	if err := os.WriteFile(runner, testRunner, 0644); err != nil {
		return err
	}
	args := []string{"--test", runner}
	if jsonOutput {
		reporter := filepath.Join(filepath.Dir(runner), "test-reporter.mjs")
		if err := os.WriteFile(reporter, testReporter, 0644); err != nil {
			return err
		}
		args = []string{"--test", "--test-reporter=" + reporter, runner}
	}
	node := exec.Command(tc.Node, args...)
	node.Dir = scriptsPath(projectRoot) // asc resolves @jm/runtime from here
	node.Env = append(os.Environ(), "JM_TEST_SPECS="+string(list), "JM_TEST_ROOT="+projectRoot, "JM_ASC="+tc.ASC)
	node.Stdout = os.Stdout
	node.Stderr = os.Stderr
	if err := node.Run(); err != nil {
		if jsonOutput {
			os.Exit(1) // the last line said so
		}
		return fmt.Errorf("tests failed")
	}
	return nil
}

func init() {
	testCmd.Flags().BoolVar(&jsonOutput, "json", false, "a JSON line per test, then the result (for tools)")
}
