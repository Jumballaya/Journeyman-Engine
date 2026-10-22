package main

import (
	"bytes"
	"errors"
	"fmt"
	"image/color"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"
)

// filesGame is a game with a few files, no engine to ask for a schema.
func filesGame(t *testing.T, dir string) string {
	t.Helper()
	scratchSettings(t) // the files lock lives with the editor's settings
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
	for _, clutter := range []string{".DS_Store", "assets/.DS_Store", "assets/Thumbs.db"} { // the OS's, left out
		os.WriteFile(filepath.Join(game, clutter), nil, 0o644)
	}
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
	want := `[{"entity":"Bat","change":"added"},{"entity":"Coin","occurrence":2,"change":"changed","changed":["TransformComponent"]},` +
		`{"entity":"Lock","change":"removed"},{"entity":"Player","change":"changed","changed":["TransformComponent"]}]`
	if got != want {
		t.Errorf("changes:\n%s\nwant\n%s", got, want)
	}
	// Literally named "Coin #2": its own entity, not the second Coin.
	before = `{"entities":[{"name":"Coin"},{"name":"Coin","components":{"A":1}},{"name":"Coin #2","components":{"B":1}}]}`
	second := sceneChanges([]byte(before), []byte(strings.Replace(before, `"A":1`, `"A":2`, 1)))
	if got := jsonOf(second); got != `[{"entity":"Coin","occurrence":2,"change":"changed","changed":["A"]}]` {
		t.Errorf("the second Coin's change: %s", got)
	}
	literal := sceneChanges([]byte(before), []byte(strings.Replace(before, `"B":1`, `"B":2`, 1)))
	if got := jsonOf(literal); got != `[{"entity":"Coin #2","change":"changed","changed":["B"]}]` {
		t.Errorf("Coin #2's change: %s", got)
	}
	if a, b := second[0].label(), literal[0].label(); a == b || a != `"Coin" #2` || b != `"Coin #2"` {
		t.Errorf("labels: %s and %s", a, b)
	}
	// A name holding a NUL (JSON allows it) still counts as its own.
	nul := `{"entities":[{"name":"Coin\u0000x","components":{"A":1}},{"name":"Coin\u0000x","components":{"A":1}}]}`
	if got := jsonOf(sceneChanges([]byte(nul), []byte(strings.Replace(nul, `"A":1}}]`, `"A":2}}]`, 1)))); got != `[{"entity":"Coin\u0000x","occurrence":2,"change":"changed","changed":["A"]}]` {
		t.Errorf("a name with a NUL: %s", got)
	}
	if got := jsonOf(sceneChanges(nil, []byte(after))); !strings.Contains(got, `{"entity":"Player","change":"added"}`) {
		t.Errorf("a new scene: %s", got)
	}
}

// Saves and moves from several jm at once: exactly one wins, the rest lose nothing.
func TestOverlappingFileChangesHaveOneWinner(t *testing.T) {
	game := filesGame(t, t.TempDir())
	race := func(n int, change func(i int) error) (wins int) {
		var wg sync.WaitGroup
		var mu sync.Mutex
		for i := range n {
			wg.Add(1)
			go func() {
				defer wg.Done()
				if change(i) == nil {
					mu.Lock()
					wins++
					mu.Unlock()
				}
			}()
		}
		wg.Wait()
		return wins
	}
	f, _ := readGameFile(game, "assets/scripts/player.ts")
	if wins := race(8, func(i int) error {
		_, err := writeGameFile(game, "assets/scripts/player.ts", fmt.Sprint("// version ", i), &f.Etag)
		return err
	}); wins != 1 {
		t.Errorf("%d saves over one version", wins)
	}
	none := ""
	if wins := race(8, func(i int) error {
		_, err := writeGameFile(game, "assets/scripts/new.ts", fmt.Sprint("// new ", i), &none)
		return err
	}); wins != 1 {
		t.Errorf("%d saves of one new file", wins)
	}
	for i := range 8 {
		os.WriteFile(filepath.Join(game, fmt.Sprintf("m%d.txt", i)), []byte(fmt.Sprint(i)), 0o644)
	}
	if wins := race(8, func(i int) error {
		_, _, err := moveGameFile(game, fmt.Sprintf("m%d.txt", i), "moved.txt")
		return err
	}); wins != 1 {
		t.Errorf("%d moves to one place", wins)
	}
	left, _ := filepath.Glob(filepath.Join(game, "m[0-9].txt"))
	if len(left) != 7 {
		t.Errorf("%d sources left, want the 7 that lost", len(left))
	}
}

// A hidden folder is hidden in any case (macOS's disks don't tell BUILD from build).
func TestHiddenFoldersAreHiddenInAnyCase(t *testing.T) {
	game := filesGame(t, t.TempDir())
	for _, path := range []string{"BUILD/out.txt", "Build/new.txt", "DIST/x.txt", "Node_Modules/x/y.js"} {
		if _, err := writeGameFile(game, path, "x", nil); err == nil {
			t.Errorf("wrote %s", path)
		}
		if _, err := readGameFile(game, path); err == nil {
			t.Errorf("read %s", path)
		}
		if _, err := deleteGameFile(game, path); err == nil {
			t.Errorf("deleted %s", path)
		}
		if _, _, err := moveGameFile(game, "scenes/main.scene.json", path); err == nil {
			t.Errorf("moved into %s", path)
		}
	}
	if data, _ := os.ReadFile(filepath.Join(game, "build/out.txt")); string(data) != "built" {
		t.Errorf("build/out.txt is %q", data)
	}
	if _, err := readGameFile(game, ".jm.json"); err != nil {
		t.Errorf(".jm.json: %v", err)
	}
}

// file_write takes text and etag only as strings: anything else changes nothing.
func TestFileWriteRefusesArgumentsThatArentText(t *testing.T) {
	game := filesGame(t, t.TempDir())
	t.Chdir(game)
	before, _ := os.ReadFile("scenes/main.scene.json")
	for _, args := range []string{`{"path":"scenes/main.scene.json"}`, `{"path":"scenes/main.scene.json","text":3}`,
		`{"path":"scenes/main.scene.json","text":null}`, `{"path":"scenes/main.scene.json","text":{}}`,
		`{"path":"scenes/main.scene.json","text":"{}","etag":5}`, `{"path":"new.txt","text":7}`, `{"path":3,"text":"x"}`} {
		if got := sceneCall(t, `{"name":"file_write","arguments":`+args+`}`); got["isError"] != true {
			t.Errorf("%s: %v", args, got)
		}
	}
	if after, _ := os.ReadFile("scenes/main.scene.json"); string(after) != string(before) || exists("new.txt") {
		t.Errorf("changed: %q", after)
	}
	if got := sceneCall(t, `{"name":"file_write","arguments":{"path":"empty.txt","text":""}}`); got["isError"] == true || !exists("empty.txt") {
		t.Errorf("an empty file: %v", got)
	}
	for _, tool := range newMCPServer().tools {
		if tool.Name == "file_write" && tool.Annotations["destructiveHint"] != true {
			t.Errorf("file_write replaces a whole file, but says %v", tool.Annotations)
		}
	}
}

// File changes take nothing from a game's .jm/ (it may lead anywhere), and nested
// games share the lock: one real file, one winner.
func TestFileChangesShareOneLockAcrossGames(t *testing.T) {
	outer := filesGame(t, t.TempDir())
	inner := filepath.Join(outer, "inner")
	os.MkdirAll(inner, 0o755)
	os.WriteFile(filepath.Join(inner, ".jm.json"), []byte("{}\n"), 0o644)
	os.WriteFile(filepath.Join(inner, "shared.txt"), []byte("v0"), 0o644)
	for round := range 8 {
		f, _ := readGameFile(inner, "shared.txt")
		var wg sync.WaitGroup
		wins := make(chan bool, 2)
		for i, at := range [][2]string{{outer, "inner/shared.txt"}, {inner, "shared.txt"}} {
			wg.Add(1)
			go func() {
				defer wg.Done()
				_, err := writeGameFile(at[0], at[1], fmt.Sprint("round ", round, " by ", i), &f.Etag)
				wins <- err == nil
			}()
		}
		wg.Wait()
		close(wins)
		n := 0
		for w := range wins {
			if w {
				n++
			}
		}
		if n != 1 {
			t.Fatalf("round %d: %d saves of one version", round, n)
		}
	}
	outside := t.TempDir()
	os.RemoveAll(filepath.Join(outer, ".jm"))
	if os.Symlink(outside, filepath.Join(outer, ".jm")) != nil {
		t.Skip("no symlinks here")
	}
	writeGameFile(outer, "../outside.txt", "x", nil)
	if _, err := writeGameFile(outer, "a.txt", "x", nil); err != nil {
		t.Errorf("a write: %v", err)
	}
	if entries, _ := os.ReadDir(outside); len(entries) != 0 {
		t.Errorf("wrote where .jm leads: %v", entries)
	}
}

// A page showing one game names it: the change lands there, whichever game is open.
func TestFileAndMemoryToolsWorkOnTheGameNamed(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"a", "b"} {
		filesGame(t, filepath.Join(home, g))
		os.WriteFile(filepath.Join(home, g, "x.txt"), []byte(g), 0o644)
	}
	t.Chdir(filepath.Join(home, "b"))
	call := func(name string, args map[string]any) toolResult {
		for _, tool := range newMCPServer().tools {
			if tool.Name == name {
				return tool.run(args)
			}
		}
		t.Fatalf("no tool %s", name)
		return toolResult{}
	}
	if got := call("file_delete", map[string]any{"path": "x.txt", "game": filepath.Join(home, "a")}); got.Failed {
		t.Fatal(got.Text)
	}
	if got := call("memory_note", map[string]any{"text": "a's note", "game": "a"}); got.Failed {
		t.Fatal(got.Text)
	}
	if exists(filepath.Join(home, "a", "x.txt")) {
		t.Error("a's file is still there")
	}
	if data, _ := os.ReadFile(filepath.Join(home, "b", "x.txt")); string(data) != "b" || exists(filepath.Join(home, "b", "design")) {
		t.Errorf("b changed: x.txt %q", data)
	}
	if data, _ := os.ReadFile(filepath.Join(home, "a", "design", "Notes.md")); !strings.Contains(string(data), "a's note") {
		t.Errorf("a's notes: %s", data)
	}
}

// A PNG is given as it is, whatever case its name ends in.
func TestAPNGIsReadAsItIsInAnyCase(t *testing.T) {
	game := filesGame(t, t.TempDir())
	t.Chdir(game)
	writePNG(t, filepath.Join(game, "art.png"), 4, 4, color.Transparent)
	want, _ := os.ReadFile(filepath.Join(game, "art.png"))
	for _, name := range []string{"ART.PNG", "Art.Png"} {
		os.WriteFile(filepath.Join(game, name), want, 0o644)
	}
	for _, name := range []string{"art.png", "ART.PNG", "Art.Png"} {
		var got toolResult
		for _, tool := range newMCPServer().tools {
			if tool.Name == "file_read" {
				got = tool.run(map[string]any{"path": name})
			}
		}
		if len(got.Images) != 1 || got.Images[0].MimeType != "image/png" || !bytes.Equal(got.Images[0].Data, want) {
			t.Errorf("%s: not the PNG as it is", name)
		}
	}
}

// A game rooted where the editor's settings live can delete them, lock and
// all: the next change makes a new lock and still has it alone.
func TestDeletingTheSettingsFromAGameKeepsOneLock(t *testing.T) {
	scratchSettings(t)
	game := os.Getenv("HOME")
	os.WriteFile(filepath.Join(game, ".jm.json"), []byte("{}\n"), 0o644)
	if _, err := writeGameFile(game, "notes.txt", "one", nil); err != nil { // makes the lock
		t.Fatal(err)
	}
	if _, err := deleteGameFile(game, "Library"); err != nil && runtime.GOOS == "darwin" {
		t.Fatal(err)
	}
	if _, err := writeGameFile(game, "notes.txt", "two", nil); err != nil {
		t.Fatalf("after the lock went: %v", err)
	}
}
