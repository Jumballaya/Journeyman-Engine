package main

import (
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"testing"

)

// nodeMajorVersion is not unit-tested because it shells to `node`. Tested
// indirectly via TestCheckBuildPrereqs (not present here — checkBuildPrereqs
// also shells out, and refactoring for injectability is out of scope for this
// task).

// ---------------------------------------------------------------------------
// validateRelativePath
// ---------------------------------------------------------------------------

func TestValidateRelativePath(t *testing.T) {
	type tc struct {
		name        string
		path        string
		wantErr     bool
		errContains string // substring expected in the error message; "" means skip the check
	}

	cases := []tc{
		// PASS cases.
		{"simple file", "foo.ts", false, ""},
		{"nested path", "a/b/c.wasm", false, ""},
		{"double-dot in middle of segment", "foo..bar", false, ""},
		{"double-dot prefix", "..foo", false, ""},
		{"dot-slash prefix collapses", "./bar", false, ""},
		{"single dot collapses to dot — allowed", ".", false, ""},

		// `foo/../bar` collapses via filepath.Clean to `bar` BEFORE the segment
		// walk, so it passes — the cleaned form contains no `..` segment and
		// the path is contained within the tree. Pin this so anyone tightening
		// the function (e.g. checking the raw input) updates the test.
		{"interior traversal collapses to safe path", "foo/../bar", false, ""},

		// FAIL cases.
		{"empty string", "", true, "empty"},
		{"parent traversal", "../foo", true, "../foo"},
		{"escaping traversal", "a/../../b", true, "a/../../b"},
	}

	if runtime.GOOS != "windows" {
		// `filepath.IsAbs` treats `/etc/passwd` as absolute on POSIX only.
		cases = append(cases, tc{"absolute posix path", "/etc/passwd", true, "/etc/passwd"})
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			err := validateRelativePath(c.path)
			if c.wantErr && err == nil {
				t.Fatalf("validateRelativePath(%q): expected error, got nil", c.path)
			}
			if !c.wantErr && err != nil {
				t.Fatalf("validateRelativePath(%q): unexpected error: %v", c.path, err)
			}
			if c.wantErr && c.errContains != "" && !strings.Contains(err.Error(), c.errContains) {
				t.Fatalf("validateRelativePath(%q): error %q should contain %q",
					c.path, err.Error(), c.errContains)
			}
		})
	}
}

// ---------------------------------------------------------------------------
// syncEmbeddedRuntime
// ---------------------------------------------------------------------------

func TestSyncEmbeddedRuntime(t *testing.T) {
	root := t.TempDir()
	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("syncEmbeddedRuntime: %v", err)
	}

	runtimeDir := filepath.Join(root, "assets/scripts/node_modules/@jm/runtime")

	// Every module index.ts re-exports must be extracted next to it.
	indexPath := filepath.Join(runtimeDir, "index.ts")
	indexBytes, err := os.ReadFile(indexPath)
	if err != nil {
		t.Fatalf("read %s: %v", indexPath, err)
	}
	modules := regexp.MustCompile(`from "\./(\w+)"`).FindAllStringSubmatch(string(indexBytes), -1)
	if len(modules) == 0 {
		t.Fatalf("index.ts re-exports nothing:\n%s", indexBytes)
	}
	for _, m := range modules {
		if _, err := os.Stat(filepath.Join(runtimeDir, m[1]+".ts")); err != nil {
			t.Fatalf("index.ts re-exports %q but it wasn't extracted: %v", m[1], err)
		}
	}

	// package.json must be extracted with @jm/runtime name.
	pkgPath := filepath.Join(runtimeDir, "package.json")
	pkgBytes, err := os.ReadFile(pkgPath)
	if err != nil {
		t.Fatalf("read %s: %v", pkgPath, err)
	}
	if !strings.Contains(string(pkgBytes), `"@jm/runtime"`) {
		t.Fatalf("package.json missing @jm/runtime name: %s", pkgBytes)
	}
}

func TestSyncEmbeddedRuntimeIsIdempotent(t *testing.T) {
	root := t.TempDir()

	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("first sync: %v", err)
	}
	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("second sync: %v", err)
	}

	// Spot-check that the well-known files survived the second invocation.
	indexPath := filepath.Join(root, "assets/scripts/node_modules/@jm/runtime/index.ts")
	if _, err := os.Stat(indexPath); err != nil {
		t.Fatalf("index.ts missing after second sync: %v", err)
	}
}

func TestSyncEmbeddedRuntimePrunesExtraneousFiles(t *testing.T) {
	root := t.TempDir()

	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("first sync: %v", err)
	}

	// Plant a file that doesn't exist in the embed. Second sync should wipe it.
	extra := filepath.Join(root, "assets/scripts/node_modules/@jm/runtime/extra.ts")
	if err := os.WriteFile(extra, []byte("// stale"), 0o644); err != nil {
		t.Fatalf("plant extra: %v", err)
	}

	if err := syncEmbeddedRuntime(root); err != nil {
		t.Fatalf("second sync: %v", err)
	}

	if _, err := os.Stat(extra); !os.IsNotExist(err) {
		t.Fatalf("extra.ts should have been pruned, stat err: %v", err)
	}

	// And the legitimate files still exist.
	if _, err := os.Stat(filepath.Join(root, "assets/scripts/node_modules/@jm/runtime/index.ts")); err != nil {
		t.Fatalf("index.ts missing after prune: %v", err)
	}
}
