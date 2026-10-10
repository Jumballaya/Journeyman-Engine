package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func TestInstallScriptLogsAndPrintsAnyFailure(t *testing.T) {
	skipOnWindows(t)
	platform := runtime.GOOS + "-" + runtime.GOARCH
	if platform == "linux-arm64" {
		t.Skip("install.sh has no linux-arm64 build")
	}
	file := "journeyman-cli-" + platform + ".tar.gz"
	from, dir := t.TempDir(), t.TempDir()
	// Checksum-valid, but under the wrong folder, and read-only so cleanup fails too.
	archive := tarGz(t, "unexpected/", "unexpected/jm")
	os.WriteFile(filepath.Join(from, file), archive, 0o644)
	os.WriteFile(filepath.Join(from, "SHA256SUMS"), fmt.Appendf(nil, "%x  %s\n", sha256.Sum256(archive), file), 0o644)
	os.Mkdir(filepath.Join(dir, "bin"), 0o755)
	old := fakeProgram(t, filepath.Join(dir, "bin"), "jm", "")

	cmd := exec.Command("sh", "../../../scripts/install.sh")
	tmp := t.TempDir()
	t.Cleanup(func() { exec.Command("chmod", "-R", "u+w", tmp).Run() })
	cmd.Env = append(os.Environ(), "JM_FROM="+from, "JM_INSTALL_DIR="+dir, "TMPDIR="+tmp)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Run(); err == nil {
		t.Fatal("install.sh succeeded with a malformed archive")
	}
	cause := "doesn't hold journeyman-cli-" + platform + "/jm"
	log, _ := os.ReadFile(filepath.Join(dir, "install.log"))
	if !strings.Contains(string(log), cause) {
		t.Errorf("install.log lacks the cause:\n%s", log)
	}
	if !strings.Contains(stderr.String(), "--- ") || !strings.Contains(stderr.String(), cause) {
		t.Errorf("the failure didn't print the log:\n%s", stderr.String())
	}
	if _, err := os.Stat(old); err != nil {
		t.Errorf("a failed install removed the previous one: %v", err)
	}
}

func tarGz(t *testing.T, names ...string) []byte {
	t.Helper()
	var buf bytes.Buffer
	gz := gzip.NewWriter(&buf)
	tw := tar.NewWriter(gz)
	for _, name := range names {
		h := &tar.Header{Name: name, Mode: 0o755}
		if strings.HasSuffix(name, "/") {
			h.Typeflag, h.Mode = tar.TypeDir, 0o555
		}
		tw.WriteHeader(h)
	}
	if err := tw.Close(); err != nil {
		t.Fatal(err)
	}
	gz.Close()
	return buf.Bytes()
}
