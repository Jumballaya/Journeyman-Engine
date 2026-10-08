package toolchain

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/sha512"
	"encoding/base64"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// tgz makes an npm-style tarball (files under package/).
func tgz(t *testing.T, files map[string]string) []byte {
	t.Helper()
	var buf bytes.Buffer
	gz := gzip.NewWriter(&buf)
	tw := tar.NewWriter(gz)
	for name, body := range files {
		tw.WriteHeader(&tar.Header{Name: "package/" + name, Mode: 0o644, Size: int64(len(body)), Typeflag: tar.TypeReg})
		tw.Write([]byte(body))
	}
	tw.Close()
	gz.Close()
	return buf.Bytes()
}

func integrity(data []byte) string {
	sum := sha512.Sum512(data)
	return "sha512-" + base64.StdEncoding.EncodeToString(sum[:])
}

// fakeRegistry serves one assemblyscript tarball and counts requests.
func fakeRegistry(t *testing.T, tarball []byte) *int {
	t.Helper()
	hits := new(int)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		*hits++
		w.Write(tarball)
	}))
	t.Cleanup(srv.Close)
	oldURL, oldPkgs := npmURL, ascPackages
	t.Cleanup(func() { npmURL, ascPackages = oldURL, oldPkgs })
	npmURL = srv.URL
	return hits
}

func TestManagedASCDownloadsOnceAndCaches(t *testing.T) {
	t.Setenv("JM_TOOLCHAIN_DIR", t.TempDir())
	tarball := tgz(t, map[string]string{"dist/asc.js": "// asc", "bin/asc.js": "// bin"})
	hits := fakeRegistry(t, tarball)
	ascPackages = []npmPackage{{"assemblyscript", ASCVersion, integrity(tarball)}}

	var log bytes.Buffer
	pkg, err := managedASC(true, &log)
	if err != nil {
		t.Fatal(err)
	}
	if got, _ := os.ReadFile(filepath.Join(pkg, "dist", "asc.js")); string(got) != "// asc" {
		t.Fatalf("dist/asc.js = %q", got)
	}
	if !strings.Contains(log.String(), "Downloading AssemblyScript") {
		t.Errorf("no progress line: %q", log.String())
	}
	if _, err := managedASC(true, io.Discard); err != nil || *hits != 1 {
		t.Fatalf("second call: err %v, %d downloads (want 1)", err, *hits)
	}
}

func TestManagedASCRejectsAChangedTarball(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("JM_TOOLCHAIN_DIR", dir)
	fakeRegistry(t, tgz(t, map[string]string{"dist/asc.js": "// tampered"}))
	ascPackages = []npmPackage{{"assemblyscript", ASCVersion, integrity([]byte("the real one"))}}

	_, err := managedASC(true, io.Discard)
	if err == nil || !strings.Contains(err.Error(), "checksum mismatch") {
		t.Fatalf("err = %v, want a checksum mismatch", err)
	}
	if entries, _ := os.ReadDir(dir); len(entries) != 0 {
		t.Errorf("cache isn't empty after a rejected download: %v", entries)
	}
}

func TestNoFetchSaysHowToGetIt(t *testing.T) {
	t.Setenv("JM_TOOLCHAIN_DIR", t.TempDir())
	_, err := managedASC(false, io.Discard)
	if err == nil || !strings.Contains(err.Error(), "jm doctor --fetch") {
		t.Fatalf("err = %v", err)
	}
}

func TestProjectASCWins(t *testing.T) {
	scripts := t.TempDir()
	own := filepath.Join(scripts, "node_modules", "assemblyscript")
	os.MkdirAll(filepath.Join(own, "dist"), 0o755)
	os.WriteFile(filepath.Join(own, "dist", "asc.js"), nil, 0o644)

	var tc Toolchain
	if err := tc.findASC(scripts, false, io.Discard); err != nil {
		t.Fatal(err)
	}
	if tc.ASC != own || tc.ASCSource != "project" {
		t.Fatalf("got %+v", tc)
	}
}

func TestArchivePathsCantEscape(t *testing.T) {
	if err := writeFile(t.TempDir(), "../evil", strings.NewReader("x"), 0o644); err == nil {
		t.Fatal("wrote outside the folder")
	}
}

func TestEveryPlatformHasAPinnedNode(t *testing.T) {
	for _, p := range []string{"darwin-arm64", "darwin-x64", "linux-x64", "linux-arm64", "win-x64"} {
		if len(nodeSHA256[p]) != 64 {
			t.Errorf("%s: no sha256", p)
		}
	}
}
