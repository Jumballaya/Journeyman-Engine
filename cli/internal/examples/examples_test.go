package examples

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// release serves a release's SHA256SUMS and examples zip (files: path → content
// under the zip's top folder), counting the zip's downloads.
func release(t *testing.T, files map[string]string, sum string) *int {
	t.Helper()
	var buf bytes.Buffer
	zw := zip.NewWriter(&buf)
	for name, content := range files {
		w, _ := zw.Create("journeyman-examples/" + name)
		w.Write([]byte(content))
	}
	zw.Close()
	if sum == "" {
		digest := sha256.Sum256(buf.Bytes())
		sum = hex.EncodeToString(digest[:])
	}
	downloads := new(int)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/v9.9.9/SHA256SUMS":
			io.WriteString(w, "abc  journeyman-cli-linux-amd64.tar.gz\n"+sum+"  journeyman-examples.zip\n")
		case "/v9.9.9/journeyman-examples.zip":
			*downloads++
			w.Write(buf.Bytes())
		default:
			http.NotFound(w, r)
		}
	}))
	t.Cleanup(server.Close)
	old := releasesURL
	releasesURL = server.URL
	t.Cleanup(func() { releasesURL = old })
	t.Setenv("HOME", t.TempDir())
	t.Setenv("USERPROFILE", os.Getenv("HOME"))
	return downloads
}

var demos = map[string]string{
	"README.md":                     "# Demos",
	"common/index.ts":               "export const shared = 1;",
	"tetris/.jm.json":               `{"name": "Tetris", "entryScene": "scenes/main.scene.json", "scriptLibraries": {"@demos/common": "../common", "local": "lib"}}`,
	"tetris/lib/x.ts":               "export const x = 1;",
	"tetris/scenes/main.scene.json": `{}`,
	"tetris/build/stale.txt":        "not a source",
	"dungeon/.jm.json":              `{"name": "Dungeon"}`,
}

func TestTheExamplesAreDownloadedOnceAndListed(t *testing.T) {
	downloads := release(t, demos, "")
	for range 2 {
		list, err := List("v9.9.9", io.Discard)
		if err != nil {
			t.Fatal(err)
		}
		if len(list) != 2 || list[0].Name != "dungeon" || list[1].Title != "Tetris" {
			t.Fatalf("listed %+v", list)
		}
	}
	if *downloads != 1 {
		t.Errorf("downloaded %d times", *downloads)
	}
}

func TestAnExampleThatDoesntMatchItsChecksumIsntUsed(t *testing.T) {
	release(t, demos, strings.Repeat("0", 64))
	if _, err := List("v9.9.9", io.Discard); err == nil || !strings.Contains(err.Error(), "checksum") {
		t.Fatalf("got %v", err)
	}
	if _, err := os.Stat(filepath.Join(os.Getenv("HOME"), ".jm", "examples", "v9.9.9")); err == nil {
		t.Error("cached it anyway")
	}
}

func TestADevelopmentJmPointsAtDemos(t *testing.T) {
	if _, err := List("dev", io.Discard); err == nil || !strings.Contains(err.Error(), "demos/") {
		t.Fatalf("got %v", err)
	}
}

func TestACopyIsAGameOfItsOwn(t *testing.T) {
	release(t, demos, "")
	to := filepath.Join(t.TempDir(), "my-tetris")
	if err := Copy("v9.9.9", "tetris", to, io.Discard); err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"scenes/main.scene.json", "lib/x.ts", "libraries/demos-common/index.ts"} {
		if _, err := os.Stat(filepath.Join(to, want)); err != nil {
			t.Errorf("no %s", want)
		}
	}
	if _, err := os.Stat(filepath.Join(to, "build")); err == nil {
		t.Error("copied build/")
	}
	data, _ := os.ReadFile(filepath.Join(to, ".jm.json"))
	var m struct {
		Name      string            `json:"name"`
		Libraries map[string]string `json:"scriptLibraries"`
	}
	json.Unmarshal(data, &m)
	if m.Name != "my-tetris" || m.Libraries["@demos/common"] != "libraries/demos-common" || m.Libraries["local"] != "lib" {
		t.Errorf("manifest %s", data)
	}
	if !strings.HasPrefix(string(data), "{\n  \"name\"") {
		t.Errorf("keys reordered: %s", data)
	}
	if err := Copy("v9.9.9", "tetris", to, io.Discard); err == nil {
		t.Error("copied over a game")
	}
	if err := Copy("v9.9.9", "nope", filepath.Join(t.TempDir(), "x"), io.Discard); err == nil {
		t.Error("copied an example that isn't there")
	}
}
