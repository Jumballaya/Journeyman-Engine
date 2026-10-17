package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const pngHead = "\x89PNG\r\n\x1a\nrest"

// A host's file server: /hero.png, /big.png (over the cap), /fake.png (HTML),
// /typed.png (served as text/html), /hop (a redirect to plain http).
func assetHost(t *testing.T) *httptest.Server {
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/hero.png":
			w.Header().Set("Content-Type", "image/png")
			w.Write([]byte(pngHead))
		case "/big.png":
			w.Write([]byte(pngHead + strings.Repeat("x", assetLimit)))
		case "/fake.png":
			w.Header().Set("Content-Type", "application/octet-stream")
			w.Write([]byte("<html>"))
		case "/typed.png":
			w.Header().Set("Content-Type", "text/html")
			w.Write([]byte(pngHead))
		case "/data.json":
			w.Write([]byte(`{"x":1}`))
		case "/hop":
			http.Redirect(w, r, "http://"+r.Host+"/hero.png", http.StatusFound)
		}
	}))
	t.Cleanup(srv.Close)
	// The test server is on loopback, which the real client refuses: this one trusts it, redirect rule kept.
	saved := assetClient
	client := srv.Client()
	client.CheckRedirect = httpsRedirects
	assetClient = client
	t.Cleanup(func() { assetClient = saved })
	return srv
}

func assetGame(t *testing.T) (string, func(args map[string]any) toolResult) {
	scratchSettings(t)
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{"name":"Pip"}`), 0o644)
	t.Chdir(root)
	var tool mcpTool
	for _, x := range newMCPServer().tools {
		if x.Name == "asset_add" {
			tool = x
		}
	}
	return root, func(args map[string]any) toolResult { return tool.run(args) }
}

func TestAssetAddSavesAnAttachedFile(t *testing.T) {
	srv := assetHost(t)
	root, add := assetGame(t)
	file := map[string]any{"download_url": srv.URL + "/hero.png", "file_id": "file_1", "file_name": "hero.png"}
	r := add(map[string]any{"file": file})
	if r.Failed || r.Structured.(map[string]any)["path"] != "assets/hero.png" {
		t.Fatalf("add: %+v", r)
	}
	if data, _ := os.ReadFile(filepath.Join(root, "assets", "hero.png")); string(data) != pngHead {
		t.Errorf("saved %q", data)
	}
	if r := add(map[string]any{"file": file}); !r.Failed || !strings.Contains(r.Text, "already there") {
		t.Errorf("over an existing file: %+v", r)
	}
	r = add(map[string]any{"file": file, "path": "assets/sprites/hero.png"})
	if r.Failed || r.Structured.(map[string]any)["replaced"] != false {
		t.Errorf("to a new folder: %+v", r)
	}
	if r := add(map[string]any{"file": file, "replace": true}); r.Failed || r.Structured.(map[string]any)["replaced"] != true {
		t.Errorf("replace: %+v", r)
	}
}

func TestAssetAddRefusesWhatIsntAGameAsset(t *testing.T) {
	srv := assetHost(t)
	root, add := assetGame(t)
	outside := t.TempDir()
	os.MkdirAll(filepath.Join(root, "assets"), 0o755)
	os.Symlink(outside, filepath.Join(root, "assets", "out"))
	os.WriteFile(filepath.Join(root, "assets", "config.json"), []byte("{}"), 0o644)
	os.Symlink(filepath.Join(root, "assets", "config.json"), filepath.Join(root, "assets", "import.json"))
	cases := map[string]map[string]any{
		"http":           {"file": map[string]any{"download_url": strings.Replace(srv.URL, "https:", "http:", 1) + "/hero.png", "file_id": "f", "file_name": "a.png"}},
		"redirect":       {"file": map[string]any{"download_url": srv.URL + "/hop", "file_id": "f"}, "path": "assets/hop.png"},
		"oversize":       {"file": map[string]any{"download_url": srv.URL + "/big.png", "file_id": "f", "file_name": "big.png"}},
		"not a png":      {"file": map[string]any{"download_url": srv.URL + "/fake.png", "file_id": "f", "file_name": "fake.png"}},
		"served as html": {"file": map[string]any{"download_url": srv.URL + "/typed.png", "file_id": "f", "file_name": "typed.png"}},
		"bad extension":  {"file": map[string]any{"download_url": srv.URL + "/hero.png", "file_id": "f", "file_name": "run.sh"}},
		"no name":        {"file": map[string]any{"download_url": srv.URL + "/hero.png", "file_id": "f"}},
		"dot name":       {"file": map[string]any{"download_url": srv.URL + "/hero.png", "file_id": "f", "file_name": ".png"}},
		"not assets":     {"file": map[string]any{"download_url": srv.URL + "/hero.png", "file_id": "f"}, "path": "scenes/x.png"},
		"escape":         {"file": map[string]any{"download_url": srv.URL + "/hero.png", "file_id": "f"}, "path": "assets/../../x.png"},
		"through a link": {"file": map[string]any{"download_url": srv.URL + "/hero.png", "file_id": "f"}, "path": "assets/out/x.png"},
		"a linked file":  {"file": map[string]any{"download_url": srv.URL + "/data.json", "file_id": "f"}, "path": "assets/import.json", "replace": true},
		"absolute link":  {"file": map[string]any{"download_url": srv.URL + "/data.json", "file_id": "f"}, "path": filepath.Join(root, "assets", "import.json"), "replace": true},
	}
	for name, args := range cases {
		r := add(args)
		if !r.Failed {
			t.Errorf("%s was saved: %+v", name, r)
		}
	}
	if entries, _ := os.ReadDir(outside); len(entries) > 0 {
		t.Errorf("wrote outside the game: %v", entries)
	}
	if entries, _ := os.ReadDir(filepath.Join(root, "assets")); len(entries) != 3 {
		t.Errorf("assets: %v", entries)
	}
	if data, _ := os.ReadFile(filepath.Join(root, "assets", "config.json")); string(data) != "{}" {
		t.Errorf("a link's target was written: %q", data)
	}
}

// The real client won't fetch from this machine (or its network), wherever the link points.
func TestAssetClientRefusesLocalAddresses(t *testing.T) {
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) { w.Write([]byte(pngHead)) }))
	defer srv.Close()
	if _, err := downloadAsset(srv.URL+"/hero.png", []string{"image/png"}); err == nil || !strings.Contains(err.Error(), "not a public address") {
		t.Errorf("fetched from loopback: %v", err)
	}
	for _, address := range []string{"10.0.0.1:443", "192.168.1.1:443", "169.254.169.254:443", "[::1]:443", "[fd00::1]:443", "0.0.0.0:443",
		"100.100.1.1:443", "198.18.0.1:443", "[::ffff:127.0.0.1]:443", "[64:ff9b::a00:1]:443", "[2002:a00:1::]:443", "224.0.0.1:443"} {
		if publicAddressOnly("tcp", address, nil) == nil {
			t.Errorf("%s is allowed", address)
		}
	}
	if err := publicAddressOnly("tcp", "93.184.216.34:443", nil); err != nil {
		t.Errorf("a public address: %v", err)
	}
}

// asset_add is a file parameter for ChatGPT, and the tools that say an outputSchema answer it.
func TestToolsDeclareTheirFilesAndOutputs(t *testing.T) {
	root := sceneGame(t)
	t.Chdir(root)
	s := newMCPServer()
	answers := map[string]map[string]any{"files": {}, "file_read": {"path": "scenes/level.scene.json"},
		"file_write": {"path": "assets/n.json", "text": "{}"}, "files_search": {"text": "Cloud"}}
	declared := 0
	for _, tool := range s.tools {
		if tool.Name == "asset_add" && jsonOf(tool.Meta["openai/fileParams"]) != `["file"]` {
			t.Errorf("asset_add fileParams: %v", tool.Meta)
		}
		if tool.OutputSchema == nil || tool.Name == "asset_add" || strings.HasPrefix(tool.Name, "settings") {
			continue
		}
		declared++
		args, ok := answers[tool.Name]
		if !ok {
			t.Errorf("%s declares an output but isn't checked here", tool.Name)
			continue
		}
		r := tool.run(args)
		if r.Failed {
			t.Fatalf("%s: %s", tool.Name, r.Text)
		}
		matchesSchema(t, tool.Name, jsonResultValue(t, r.Structured), tool.OutputSchema)
	}
	if declared != len(answers) {
		t.Errorf("checked %d of %d", declared, len(answers))
	}
}

// jsonResultValue is v as a host gets it (decoded JSON).
func jsonResultValue(t *testing.T, v any) map[string]any {
	var out map[string]any
	if err := json.Unmarshal([]byte(jsonOf(v)), &out); err != nil {
		t.Fatal(err)
	}
	return out
}

// matchesSchema checks an object's required keys and its properties' types.
func matchesSchema(t *testing.T, name string, got map[string]any, schema map[string]any) {
	t.Helper()
	required, _ := schema["required"].([]string)
	for _, key := range required {
		if _, ok := got[key]; !ok {
			t.Errorf("%s: no %s in %v", name, key, got)
		}
	}
	for key, p := range schema["properties"].(map[string]any) {
		v, ok := got[key]
		if !ok {
			continue
		}
		kind := map[string]bool{}
		switch v.(type) {
		case string:
			kind["string"] = true
		case bool:
			kind["boolean"] = true
		case float64:
			kind["number"], kind["integer"] = true, v.(float64) == float64(int64(v.(float64)))
		case []any:
			kind["array"] = true
		case map[string]any:
			kind["object"] = true
		}
		if want := p.(map[string]any)["type"].(string); !kind[want] {
			t.Errorf("%s: %s is %T, not %s", name, key, v, want)
		}
	}
	for key := range got {
		if _, ok := schema["properties"].(map[string]any)[key]; !ok {
			t.Errorf("%s: %s isn't in its outputSchema", name, key)
		}
	}
}
