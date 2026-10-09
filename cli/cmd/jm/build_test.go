package main

import (
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"regexp"
	"runtime"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

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

// ---------------------------------------------------------------------------
// syncLibrary / editManifest
// ---------------------------------------------------------------------------

func TestSyncLibraryCopiesSourcesAndAddsAPackage(t *testing.T) {
	root := t.TempDir()
	game := filepath.Join(root, "game")
	lib := filepath.Join(root, "common")
	for _, f := range []string{filepath.Join(lib, "index.ts"), filepath.Join(lib, "ui", "dialog.ts"),
		filepath.Join(lib, "node_modules", "skip.ts")} {
		if err := os.MkdirAll(filepath.Dir(f), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(f, []byte("export const x = 1;"), 0644); err != nil {
			t.Fatal(err)
		}
	}
	if err := syncLibrary(game, "@demos/common", "../common"); err != nil {
		t.Fatalf("syncLibrary: %v", err)
	}
	dst := filepath.Join(game, scriptsPkgDir, "node_modules", "@demos", "common")
	for _, want := range []string{"index.ts", "ui/dialog.ts", "package.json"} {
		if _, err := os.Stat(filepath.Join(dst, want)); err != nil {
			t.Errorf("missing %s: %v", want, err)
		}
	}
	if _, err := os.Stat(filepath.Join(dst, "node_modules")); err == nil {
		t.Error("the library's node_modules should not be copied")
	}
	if err := syncLibrary(game, "@x/missing", "../nowhere"); err == nil {
		t.Error("a missing folder should be an error")
	}
}

// The name is a folder syncLibrary wipes, so a manifest can't aim it anywhere else.
func TestSyncLibraryRejectsNamesThatAreNotPackages(t *testing.T) {
	root := t.TempDir()
	game := filepath.Join(root, "game")
	if err := os.MkdirAll(filepath.Join(root, "common"), 0755); err != nil {
		t.Fatal(err)
	}
	keep := filepath.Join(game, "keep.txt")
	if err := os.MkdirAll(game, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(keep, []byte("x"), 0644); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"../../..", "..", ".", "", "a/b", "@x/../../y", "/abs", "@jm/runtime", "Upper", `a\b`} {
		if err := syncLibrary(game, name, "../common"); err == nil {
			t.Errorf("syncLibrary accepted %q", name)
		}
	}
	if _, err := os.Stat(keep); err != nil {
		t.Fatalf("the project was touched: %v", err)
	}
}

func TestEditManifestReplacesAssetsAndKeepsTheRest(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, ".jm.json")
	if err := os.WriteFile(src, []byte(`{"name":"G","assets":["assets/*.png"],"config":{"ui":{"defaultFont":"f.ttf"}}}`), 0644); err != nil {
		t.Fatal(err)
	}
	dst := filepath.Join(dir, "build", ".jm.json")
	setAssets := func(raw map[string]any) { raw["assets"] = []string{"assets/a.png", "assets/b.png"} }
	if err := editManifest(src, dst, setAssets); err != nil {
		t.Fatal(err)
	}
	data, _ := os.ReadFile(dst)
	text := string(data)
	for _, want := range []string{`"assets/a.png"`, `"assets/b.png"`, `"defaultFont": "f.ttf"`, `"name": "G"`} {
		if !strings.Contains(text, want) {
			t.Errorf("built manifest lacks %s:\n%s", want, text)
		}
	}
	if strings.Contains(text, "*") {
		t.Errorf("built manifest still has a pattern:\n%s", text)
	}
}

func TestAscOutputBecomesDiagnostics(t *testing.T) {
	output := `ERROR TS2554: Expected 4 arguments, but got 16.
    :
 10 │     log("frame=", frame.toString(),
    │     ~~~~~~~~~~~~~~
    └─ in ../../walker.ts(10,5)

WARNING AS201: Conversion from type 'f64' to 'f32' will require an explicit cast.
    └─ in ../../util.ts(3,9)

FAILURE 1 compile error(s)`
	got := parseAsc(output, func(p string) string { return "assets/scripts/" + strings.TrimPrefix(p, "../../") })
	want := []Diagnostic{
		{Level: "error", Category: "script", Message: "TS2554: Expected 4 arguments, but got 16.", File: "assets/scripts/walker.ts", Line: 10, Column: 5},
		{Level: "warning", Category: "script", Message: "AS201: Conversion from type 'f64' to 'f32' will require an explicit cast.", File: "assets/scripts/util.ts", Line: 3, Column: 9},
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %+v\nwant %+v", got, want)
	}
}

func TestReferencedScriptsAreTheOnesContentNames(t *testing.T) {
	chdir(t, t.TempDir())
	os.MkdirAll("scenes", 0o755)
	os.MkdirAll("assets/data", 0o755)
	os.WriteFile("scenes/main.scene.json", []byte(`{"entities": [{"components": {"ScriptComponent": {"script": "assets/scripts/ball.ts"}}}]}`), 0o644)
	os.WriteFile("assets/data/waves.json", []byte(`{"waves": [{"boss": "assets/scripts/boss.ts"}]}`), 0o644)
	os.MkdirAll("assets/scripts/lib", 0o755)
	os.WriteFile("assets/scripts/lib/rules.ts", []byte(`export const x = 1; // no paths here`), 0o644)
	os.WriteFile("assets/scripts/spawner.ts", []byte(`spawn("orb", 0, 0, new Overrides().set("ScriptComponent", "script", "assets/scripts/orb.ts"));`), 0o644)
	os.WriteFile(".jm.json", []byte(`{"net": {"server": {"scripts": ["assets/scripts/server/matchmaker.ts"]}}}`), 0o644)
	got := referencedScripts([]string{".jm.json", "scenes/main.scene.json", "assets/data/waves.json", "assets/scripts/lib/rules.ts",
		"assets/scripts/spawner.ts", "missing.json"})
	want := map[string]bool{"assets/scripts/ball.ts": true, "assets/scripts/boss.ts": true, "assets/scripts/orb.ts": true,
		"assets/scripts/server/matchmaker.ts": true}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %v, want %v", got, want)
	}
}

func TestScriptNameProblemsFindsTyposNotBuiltNames(t *testing.T) {
	chdir(t, t.TempDir())
	os.MkdirAll("assets/scripts", 0o755)
	os.WriteFile("assets/scripts/game.ts", []byte(`spawn("brik", 1, 2);
spawn("brick", 1, 2); spawn("pickup_" + kind, 0, 0);
Scene.load("levle2");
Scene.load("scenes/level2.scene.json");
`), 0o644)
	man := manifest.GameManifest{
		Scenes: []string{"scenes/level2.scene.json"},
		Assets: []string{"assets/scripts/game.ts", "assets/prefabs/brick.prefab.json"},
	}
	got := []string{}
	for _, d := range scriptNameProblems(man) {
		got = append(got, fmt.Sprintf("%d:%d %s", d.Line, d.Column, d.Message))
	}
	want := []string{
		`1:8 no prefab named "brik" in .jm.json (did you mean "brick"?)`,
		`3:13 no scene named "levle2" in .jm.json (did you mean "level2"?)`,
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("got %q", got)
	}
}
