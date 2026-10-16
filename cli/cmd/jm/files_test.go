package main

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// filesGame is a game with a few files, no engine to ask for a schema.
func filesGame(t *testing.T, dir string) string {
	t.Helper()
	t.Setenv("JM_ENGINE", filepath.Join(t.TempDir(), "none"))
	for path, text := range map[string]string{
		".jm.json":                 "{\n  \"name\": \"Pip\"\n}\n",
		"scenes/main.scene.json":   "{\n  \"entities\": []\n}\n",
		"assets/scripts/player.ts": "// the Player moves\nexport function update(): void {}\n",
		"build/out.txt":            "built",
		".jm/plays/p1.txt":         "play",
	} {
		os.MkdirAll(filepath.Join(dir, filepath.Dir(path)), 0o755)
		os.WriteFile(filepath.Join(dir, path), []byte(text), 0o644)
	}
	return dir
}

func git(t *testing.T, dir string, args ...string) {
	t.Helper()
	cmd := exec.Command("git", append([]string{"-C", dir, "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"}, args...)...)
	cmd.Env = append(os.Environ(), "GIT_CONFIG_GLOBAL="+os.DevNull, "GIT_CONFIG_NOSYSTEM=1")
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("git %v: %v\n%s", args, err, out)
	}
}

func paths(list []projectEntry) string {
	var out []string
	for _, f := range list {
		out = append(out, f.Status+":"+f.Path)
	}
	return strings.Join(out, " ")
}

// A game inside a bigger repository: git's marks are by path from the game, hidden folders left out.
func TestFilesListMarksWhatGitSaysChanged(t *testing.T) {
	if _, err := exec.LookPath("git"); err != nil {
		t.Skip("no git")
	}
	t.Setenv("GIT_CONFIG_GLOBAL", os.DevNull)
	repo := t.TempDir()
	game := filesGame(t, filepath.Join(repo, "games", "pip"))
	git(t, repo, "init", "-q")
	git(t, repo, "add", ".")
	git(t, repo, "commit", "-qm", "start")
	os.WriteFile(filepath.Join(game, "scenes/main.scene.json"), []byte("{}\n"), 0o644)
	os.Remove(filepath.Join(game, "assets/scripts/player.ts"))
	os.WriteFile(filepath.Join(game, "notes.md"), []byte("hi"), 0o644)
	list, err := listFiles(game)
	if err != nil {
		t.Fatal(err)
	}
	want := ":.jm.json D:assets/scripts/player.ts ?:notes.md M:scenes/main.scene.json"
	if got := paths(list); got != want {
		t.Errorf("files:\n%s\nwant\n%s", got, want)
	}
}

func TestFilesListOutsideARepositoryHasNoMarks(t *testing.T) {
	game := filesGame(t, t.TempDir())
	list, _ := listFiles(game)
	if got := paths(list); got != ":.jm.json :assets/scripts/player.ts :scenes/main.scene.json" {
		t.Errorf("files: %s", got)
	}
}

func TestFileWritesSaveOnlyOverTheVersionRead(t *testing.T) {
	game := filesGame(t, t.TempDir())
	f, err := readGameFile(game, "scenes/main.scene.json")
	if err != nil || f.Text == "" || len(f.Problems) > 0 {
		t.Fatalf("read: %+v %v", f, err)
	}
	done, err := writeGameFile(game, "scenes/main.scene.json", `{"entities":[{"name":"A"}]}`, &f.Etag)
	if err != nil {
		t.Fatal(err)
	}
	if done.Text != "{\n  \"entities\": [\n    {\n      \"name\": \"A\"\n    }\n  ]\n}\n" || done.Etag == f.Etag {
		t.Errorf("saved in jm fmt's layout with a new etag: %+v", done)
	}
	if _, err := writeGameFile(game, "scenes/main.scene.json", "{}", &f.Etag); !errors.Is(err, errChanged) {
		t.Errorf("an old etag: %v, want errChanged", err)
	}
	none := ""
	if _, err := writeGameFile(game, "scenes/main.scene.json", "{}", &none); !errors.Is(err, errChanged) {
		t.Errorf(`etag "" over a file: %v, want errChanged`, err)
	}
	made, err := writeGameFile(game, "scenes/new/level.scene.json", "{}", &none)
	if err != nil || !made.Created {
		t.Errorf(`etag "" makes a new file and its folders: %+v %v`, made, err)
	}
	if _, err := writeGameFile(game, "data.json", `{"a":`, nil); err == nil || exists(filepath.Join(game, "data.json")) {
		t.Errorf("JSON that doesn't parse is saved: %v", err)
	}
	if d, err := writeGameFile(game, "assets/scripts/player.ts", "x", nil); err != nil || d.Text != "x" {
		t.Errorf("text is saved as given: %+v %v", d, err)
	}
}

func TestFileReadSaysWhatsWrong(t *testing.T) {
	game := filesGame(t, t.TempDir())
	os.WriteFile(filepath.Join(game, "scenes/bad.scene.json"), []byte(`{"entities":[]}`), 0o644)
	os.WriteFile(filepath.Join(game, "scenes/broken.scene.json"), []byte(`{"entities":[`), 0o644)
	f, _ := readGameFile(game, "scenes/bad.scene.json")
	if len(f.Problems) != 1 || !strings.Contains(f.Problems[0], "jm fmt") {
		t.Errorf("unformatted: %v", f.Problems)
	}
	f, _ = readGameFile(game, "scenes/broken.scene.json")
	if len(f.Problems) != 1 || !strings.Contains(f.Problems[0], "not valid JSON") {
		t.Errorf("broken: %v", f.Problems)
	}
	os.WriteFile(filepath.Join(game, "art.png"), []byte{0x89, 'P', 'N', 'G', 0, 1}, 0o644)
	if f, _ := readGameFile(game, "art.png"); !f.Binary || f.Text != "" || f.Size != 6 {
		t.Errorf("binary: %+v", f)
	}
}

func TestFilePathsStayInTheGame(t *testing.T) {
	game := filesGame(t, t.TempDir())
	outside := t.TempDir()
	os.Symlink(outside, filepath.Join(game, "link"))
	os.Symlink(filepath.Join(outside, "new.txt"), filepath.Join(game, "dangling.txt"))
	os.Symlink(filepath.Join(game, ".jm"), filepath.Join(game, "alias"))
	os.MkdirAll(filepath.Join(outside, "sub"), 0o755)
	os.Symlink(filepath.Join(outside, "sub"), filepath.Join(game, "deep"))
	for _, path := range []string{"../x.txt", "/etc/hosts", "link/x.txt", ".git/config", "build/out.txt", ".jm/plays/p1.txt", "",
		"dangling.txt", "alias/plays/p1.txt", game + "/deep/../.jm.json"} {
		if _, err := writeGameFile(game, path, "x", nil); err == nil {
			t.Errorf("wrote %q", path)
		}
		if _, err := readGameFile(game, path); err == nil {
			t.Errorf("read %q", path)
		}
	}
	if entries, _ := os.ReadDir(outside); len(entries) != 1 {
		t.Errorf("wrote outside the game: %v", entries)
	}
}

func TestFileMovesAndDeletes(t *testing.T) {
	game := filesGame(t, t.TempDir())
	if _, to, err := moveGameFile(game, "assets/scripts", "src/scripts"); err != nil || to != "src/scripts" || !exists(filepath.Join(game, "src/scripts/player.ts")) {
		t.Fatalf("moved a folder: %v", err)
	}
	for _, c := range [][2]string{{"src", "src/inner"}, {".jm.json", "game.json"}, {"src/scripts/player.ts", "scenes/main.scene.json"}, {"nope.ts", "x.ts"}} {
		if _, _, err := moveGameFile(game, c[0], c[1]); err == nil {
			t.Errorf("moved %s to %s", c[0], c[1])
		}
	}
	if _, err := deleteGameFile(game, "src"); err != nil || exists(filepath.Join(game, "src")) {
		t.Errorf("delete a folder: %v", err)
	}
	if _, err := deleteGameFile(game, "src"); err != nil {
		t.Errorf("deleting what's gone does nothing: %v", err)
	}
	for _, path := range []string{".jm.json", "."} {
		if _, err := deleteGameFile(game, path); err == nil || !exists(filepath.Join(game, ".jm.json")) {
			t.Errorf("deleted %q", path)
		}
	}
}

func TestFilesSearchFindsNamesAndLines(t *testing.T) {
	game := filesGame(t, t.TempDir())
	got := searchFiles(game, "PLAYER", 10)
	if len(got.Hits) != 2 || got.Hits[0] != (searchHit{Path: "assets/scripts/player.ts"}) ||
		got.Hits[1] != (searchHit{Path: "assets/scripts/player.ts", Line: 1, Text: "// the Player moves"}) || got.Truncated {
		t.Errorf("hits: %+v", got)
	}
	if got := searchFiles(game, "e", 2); len(got.Hits) != 2 || !got.Truncated {
		t.Errorf("limited: %+v", got)
	}
	if got := searchFiles(game, "built", 10); len(got.Hits) != 0 {
		t.Errorf("searched build/: %+v", got)
	}
}

// Over MCP: a stale etag says what to do; files gives the tree to a widget.
func TestFileToolsOverMCP(t *testing.T) {
	game := filesGame(t, t.TempDir())
	t.Chdir(game)
	stale := sceneCall(t, `{"name":"file_write","arguments":{"path":"scenes/main.scene.json","text":"{}","etag":"0000"}}`)
	if stale["isError"] != true || !strings.Contains(jsonOf(stale), "file_read it again") {
		t.Errorf("stale write: %v", stale)
	}
	list := sceneCall(t, `{"name":"files","arguments":{}}`)
	s := list["structuredContent"].(map[string]any)
	if s["game"] != "Pip" || len(s["files"].([]any)) != 3 {
		t.Errorf("files: %v", list)
	}
}

// A scene's changes, per entity: by name, children too, a second of a name numbered.
func TestSceneChangesArePerEntity(t *testing.T) {
	before := `{"entities":[{"name":"Player","components":{"TransformComponent":{"position":[0,0]},"SpriteComponent":{}}},
		{"name":"Coin","prefab":"p.json"},{"name":"Coin","prefab":"p.json"},
		{"name":"Door","components":{},"children":[{"name":"Lock","components":{}}]}]}`
	after := `{"entities":[{"name":"Player","components":{"TransformComponent":{"position":[5,0]},"SpriteComponent":{}}},
		{"name":"Coin","prefab":"p.json"},{"name":"Coin","prefab":"p.json","overrides":{"TransformComponent":{}}},
		{"name":"Door","components":{}},{"name":"Bat","components":{}}]}`
	got := jsonOf(sceneChanges([]byte(before), []byte(after)))
	want := `[{"entity":"Bat","change":"added"},{"entity":"Coin #2","change":"changed","changed":["TransformComponent"]},` +
		`{"entity":"Lock","change":"removed"},{"entity":"Player","change":"changed","changed":["TransformComponent"]}]`
	if got != want {
		t.Errorf("changes:\n%s\nwant\n%s", got, want)
	}
	if got := jsonOf(sceneChanges(nil, []byte(after))); !strings.Contains(got, `{"entity":"Player","change":"added"}`) {
		t.Errorf("a new scene: %s", got)
	}
}
