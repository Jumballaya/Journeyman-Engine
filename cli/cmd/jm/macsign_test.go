package main

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const fakeDevID = "Developer ID Application: Pat Example (ABCDE12345)"

// fakeMacTools puts stand-ins for security, codesign, ditto and xcrun first on
// PATH; each logs its arguments. notarytool answers with verdict.
func fakeMacTools(t *testing.T, identities []string, verdict string) (logPath string) {
	t.Helper()
	dir := t.TempDir()
	logPath = filepath.Join(dir, "calls.log")
	var listing strings.Builder
	for i, id := range identities {
		listing.WriteString("  " + string(rune('1'+i)) + ") " + strings.Repeat("AB", 20) + ` "` + id + `"` + "\n")
	}
	scripts := map[string]string{
		"security": "cat <<'EOF'\n" + listing.String() + "EOF\n",
		"codesign": "",
		"ditto":    "",
		"xcrun":    `if [ "$1" = notarytool ]; then echo '{"id":"sub-1","status":"` + verdict + `","message":"Processing complete"}'; fi` + "\n",
	}
	for name, body := range scripts {
		script := "#!/bin/sh\necho \"" + name + " $*\" >> " + logPath + "\n" + body
		if err := os.WriteFile(filepath.Join(dir, name), []byte(script), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	t.Setenv("PATH", dir+string(os.PathListSeparator)+"/bin:/usr/bin")
	return logPath
}

func TestSignAutoPicksTheOneDeveloperID(t *testing.T) {
	fakeMacTools(t, []string{"Apple Development: Pat Example (XYZ)", fakeDevID}, "Accepted")
	if got, err := resolveIdentity("auto"); err != nil || got != fakeDevID {
		t.Fatalf("auto = %q, %v", got, err)
	}
	if got, _ := resolveIdentity(""); got != "-" {
		t.Fatalf("no --sign is ad hoc, got %q", got)
	}

	other := "Developer ID Application: Someone Else (ZZZZZ99999)"
	fakeMacTools(t, []string{fakeDevID, other}, "Accepted")
	if _, err := resolveIdentity("auto"); err == nil || !strings.Contains(err.Error(), other) {
		t.Fatalf("two identities: want an error listing them, got %v", err)
	}
	fakeMacTools(t, nil, "Accepted")
	if _, err := resolveIdentity("auto"); err == nil || !strings.Contains(err.Error(), "no Developer ID") {
		t.Fatalf("none: %v", err)
	}
}

func TestSigningFlagsAreCheckedBeforeBuilding(t *testing.T) {
	fakeMacTools(t, []string{fakeDevID}, "Accepted")
	bad := map[string]exportOptions{
		"notarize a bare binary": {target: "darwin-arm64", bare: true, notarize: "p"},
		"notarize the server":    {target: "darwin-arm64", server: true, notarize: "p"},
		"sign for linux":         {target: "linux-amd64", sign: "auto"},
	}
	for name, opts := range bad {
		if err := opts.resolve(); err == nil {
			t.Errorf("%s: want an error", name)
		}
	}
	opts := exportOptions{target: "darwin-arm64", notarize: "p"}
	if err := opts.resolve(); err != nil || opts.sign != fakeDevID {
		t.Fatalf("--notarize alone signs with the Developer ID: %q %v", opts.sign, err)
	}
}

// exportFixture is a built project and a stand-in Mach-O engine.
func exportFixture(t *testing.T) exportOptions {
	dir := t.TempDir()
	buildDir := filepath.Join(dir, "build")
	writeManifest(t, buildDir, "Sky Hop")
	writeFile(t, filepath.Join(buildDir, "scenes/level1.scene.json"), []byte(`{"entities":[]}`))
	player := filepath.Join(dir, "journeyman_engine")
	writeFile(t, player, append(binary.LittleEndian.AppendUint32(nil, 0xfeedfacf), make([]byte, 64)...))
	return exportOptions{buildDir: buildDir, outDir: filepath.Join(dir, "dist"), player: player, target: "darwin-arm64"}
}

func TestMacAppKeepsTheGameInResourcesAndIsSignedThenNotarized(t *testing.T) {
	log := fakeMacTools(t, []string{fakeDevID}, "Accepted")
	opts := exportFixture(t)
	opts.sign, opts.notarize = "auto", "jm-profile"
	if err := opts.resolve(); err != nil {
		t.Fatal(err)
	}
	if err := runExport(opts, &bytes.Buffer{}); err != nil {
		t.Fatal(err)
	}
	app := filepath.Join(opts.outDir, "Sky Hop.app")
	exe, _ := os.ReadFile(filepath.Join(app, "Contents/MacOS/Sky Hop"))
	player, _ := os.ReadFile(opts.player)
	if !bytes.Equal(exe, player) {
		t.Error("the app's executable should be the engine as is")
	}
	if _, err := readArchive(t, filepath.Join(app, "Contents/Resources/game.jm")).Read(".jm.json"); err != nil {
		t.Errorf("game.jm: %v", err)
	}
	calls, _ := os.ReadFile(log)
	staged := filepath.Join(opts.outDir, ".exporting", "Sky Hop.app")
	for _, want := range []string{
		"codesign --force --sign " + fakeDevID + " --options runtime --timestamp " + staged,
		"xcrun notarytool submit ",
		"--keychain-profile jm-profile --wait",
		"xcrun stapler staple " + staged,
	} {
		if !strings.Contains(string(calls), want) {
			t.Errorf("missing call %q in:\n%s", want, calls)
		}
	}
}

func TestRejectedNotarizationFailsAndKeepsThePreviousExport(t *testing.T) {
	fakeMacTools(t, []string{fakeDevID}, "Invalid")
	opts := exportFixture(t)
	previous := filepath.Join(opts.outDir, "Sky Hop.app", "old")
	writeFile(t, previous, []byte("x"))
	opts.notarize = "jm-profile"
	if err := opts.resolve(); err != nil {
		t.Fatal(err)
	}
	err := runExport(opts, &bytes.Buffer{})
	if err == nil || !strings.Contains(err.Error(), "notarytool log sub-1 --keychain-profile jm-profile") {
		t.Fatalf("want the rejection and how to see why, got %v", err)
	}
	if _, err := os.Stat(previous); err != nil {
		t.Error("a failed export should leave the previous one whole")
	}
}

func TestAdHocIsTheDefault(t *testing.T) {
	log := fakeMacTools(t, nil, "Accepted")
	opts := exportFixture(t)
	if err := opts.resolve(); err != nil {
		t.Fatal(err)
	}
	if err := runExport(opts, &bytes.Buffer{}); err != nil {
		t.Fatal(err)
	}
	calls, _ := os.ReadFile(log)
	if !strings.Contains(string(calls), "codesign --force --sign - ") || strings.Contains(string(calls), "runtime") ||
		strings.Contains(string(calls), "notarytool") {
		t.Fatalf("want one ad hoc signature, got:\n%s", calls)
	}
}
