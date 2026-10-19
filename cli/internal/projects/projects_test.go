package projects

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"
	"sync"
	"testing"
)

func scratchSettings(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("HOME", dir)
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_DATA_HOME", dir)
}

func makeGame(t *testing.T, dir, name string) string {
	t.Helper()
	os.MkdirAll(dir, 0o755)
	os.WriteFile(filepath.Join(dir, ".jm.json"), []byte(`{"name":"`+name+`","scenes":["scenes/main.scene.json"]}`), 0o644)
	return canonical(dir)
}

func names(list []Project) string {
	var out []string
	for _, p := range list {
		out = append(out, p.Name)
	}
	return strings.Join(out, ",")
}

func TestTheListIsTheEditorsRecentsThenTheGamesFolder(t *testing.T) {
	scratchSettings(t)
	root := t.TempDir()
	games := filepath.Join(root, "games")
	inFolder := makeGame(t, filepath.Join(games, "a"), "A")
	elsewhere := makeGame(t, filepath.Join(root, "work", "b"), "B")
	// As the editor's rememberProject writes it.
	os.MkdirAll(filepath.Dir(File()), 0o755)
	recents, _ := json.Marshal([]map[string]any{
		{"path": elsewhere, "name": "old name", "opened": 1700000000},
		{"path": filepath.Join(root, "gone"), "name": "Gone", "opened": 1600000000}})
	os.WriteFile(File(), recents, 0o644)

	list := List(games)
	if names(list) != "B,Gone,A" {
		t.Fatalf("list: %s", names(list))
	}
	if list[0].Opened != 1700000000 || list[0].Scenes[0] != "scenes/main.scene.json" || list[0].Missing {
		t.Errorf("B: %+v", list[0])
	}
	if !list[1].Missing || list[2].Opened != 0 {
		t.Errorf("Gone %+v, A %+v", list[1], list[2])
	}

	if err := Remember(inFolder); err != nil {
		t.Fatal(err)
	}
	if got := names(List(games)); got != "A,B,Gone" {
		t.Errorf("after opening A: %s", got)
	}
	if err := Forget(filepath.Join(root, "gone")); err != nil {
		t.Error(err)
	}
	if got := names(List(games)); got != "A,B" {
		t.Errorf("after forgetting Gone: %s", got)
	}
}

func TestScanAddsGamesButNotTheirBuildsOrPlays(t *testing.T) {
	scratchSettings(t)
	root := t.TempDir()
	a := makeGame(t, filepath.Join(root, "projects", "a"), "A")
	makeGame(t, filepath.Join(root, "projects", "a", "build"), "A built")
	makeGame(t, filepath.Join(root, "projects", "a", "nested"), "Inside A")
	makeGame(t, filepath.Join(root, ".hidden", "c"), "Hidden")
	b := makeGame(t, filepath.Join(root, "projects", "deep", "b"), "B")

	added, err := Scan(root)
	if err != nil || len(added) != 2 || added[0] != a || added[1] != b {
		t.Fatalf("scan: %v %v", added, err)
	}
	if again, _ := Scan(root); len(again) != 0 {
		t.Errorf("a second scan added %v", again)
	}
	if got := names(List(filepath.Join(root, "none"))); got != "A,B" {
		t.Errorf("list: %s", got)
	}
}

func TestTheThumbIsTheNewestPlaysElseAGoldenFrame(t *testing.T) {
	scratchSettings(t)
	dir := makeGame(t, filepath.Join(t.TempDir(), "g"), "G")
	if err := Add(dir); err != nil {
		t.Fatal(err)
	}
	if thumb := List("")[0].Thumb; thumb != "" {
		t.Errorf("no pictures, but thumb %s", thumb)
	}
	golden := filepath.Join(dir, "tests", "golden", "title", "frame_00060.png")
	os.MkdirAll(filepath.Dir(golden), 0o755)
	os.WriteFile(golden, []byte("png"), 0o644)
	if thumb := List("")[0].Thumb; thumb != golden {
		t.Errorf("thumb %s, want the golden frame", thumb)
	}
	play := filepath.Join(dir, ".jm", "plays", "20261015-120000")
	os.MkdirAll(filepath.Join(play, "thumbs"), 0o755)
	os.WriteFile(filepath.Join(play, "session.json"), []byte(`{"started":"2026-10-15T12:00:00"}`), 0o644)
	for _, f := range []string{"00000060.jpg", "00000120.jpg"} {
		os.WriteFile(filepath.Join(play, "thumbs", f), []byte("jpg"), 0o644)
	}
	if thumb := List("")[0].Thumb; filepath.Base(thumb) != "00000120.jpg" {
		t.Errorf("thumb %s, want the play's last", thumb)
	}
}

func TestOverlappingChangesAllKeepTheirEntries(t *testing.T) {
	scratchSettings(t)
	root := t.TempDir()
	var wg sync.WaitGroup
	for i := range 40 {
		dir := makeGame(t, filepath.Join(root, fmt.Sprint(i)), fmt.Sprint(i))
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := Add(dir); err != nil {
				t.Error(err)
			}
		}()
	}
	wg.Wait()
	if n := len(List("")); n != 40 {
		t.Errorf("%d projects listed, want all 40", n)
	}
}

func TestScanningAMissingFolderFailsAndKeepsTheList(t *testing.T) {
	scratchSettings(t)
	dir := makeGame(t, filepath.Join(t.TempDir(), "g"), "G")
	if err := Add(dir); err != nil {
		t.Fatal(err)
	}
	before, _ := os.ReadFile(File())
	if _, err := Scan(filepath.Join(t.TempDir(), "nowhere")); err == nil {
		t.Error("scanning a missing folder succeeded")
	}
	if after, _ := os.ReadFile(File()); string(after) != string(before) {
		t.Errorf("the list changed: %s", after)
	}
}

// The person's games are found in their home's usual folders, not in Library or too deep.
func TestFindGamesLooksThroughHome(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	for _, dir := range []string{"Documents/a", "Desktop/b", "Developer/x/y/c", "Library/Caches/d", "deep/1/2/3/4/e", "Documents/a/node_modules/f"} {
		makeGame(t, filepath.Join(home, dir), filepath.Base(dir))
	}
	added, err := FindGames(home)
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	for _, dir := range added {
		names = append(names, filepath.Base(dir))
	}
	slices.Sort(names)
	if strings.Join(names, ",") != "a,b,c" {
		t.Errorf("found %v", names)
	}
}

// A variant's or sandbox's folder says whose copy it is; a game that only sits
// under such a path, or a sandbox's subfolder, doesn't.
func TestCopyOf(t *testing.T) {
	root := t.TempDir()
	game := filepath.Join(root, "repo", "games", "pip")
	for _, d := range []string{game, filepath.Join(game, ".jm", "sandboxes", "s"), filepath.Join(game, ".jm", "variants", "v"), filepath.Join(game, ".jm", "variants", "v", "games", "pip"),
		filepath.Join(root, "archive", ".jm", "variants", "Mine")} {
		os.MkdirAll(d, 0o755)
		os.WriteFile(filepath.Join(d, ".jm.json"), []byte(`{}`), 0o644)
	}
	for dir, want := range map[string][3]string{
		filepath.Join(game, ".jm", "sandboxes", "s"):                {game, "sandbox", "s"},
		filepath.Join(game, ".jm", "variants", "v"):                 {game, "variant", "v"},
		filepath.Join(game, ".jm", "variants", "v", "games", "pip"): {}, // no repository: the variant is its folder
		filepath.Join(game, ".jm", "sandboxes", "s", "scenes"):      {},
		filepath.Join(root, "archive", ".jm", "variants", "Mine"):   {},
		game: {},
	} {
		g, kind, name := CopyOf(dir)
		if [3]string{g, kind, name} != want {
			t.Errorf("%s: got %q %q %q, want %q", dir, g, kind, name, want)
		}
	}
}

// A copy dropped since it was opened is still its game's (not a missing game);
// an open copy nobody remembered is listed, and so is its game.
func TestCopyDescribedEvenGone(t *testing.T) {
	game := t.TempDir()
	os.WriteFile(filepath.Join(game, ".jm.json"), []byte(`{}`), 0o644)
	gone := filepath.Join(game, ".jm", "sandboxes", "old")
	if p := describe(entry{Path: gone}); p.CopyOf == "" || p.Copy != "old" || p.CopyKind != "sandbox" {
		t.Errorf("a dropped sandbox: %+v", p)
	}
	here := filepath.Join(game, ".jm", "variants", "v")
	os.MkdirAll(here, 0o755)
	os.WriteFile(filepath.Join(here, ".jm.json"), []byte(`{"name":"V"}`), 0o644)
	list := WithCopies(nil, here)
	if len(list) != 2 || list[0].Copy != "v" || list[0].Missing || list[1].Path != canonical(game) || list[1].CopyOf != "" {
		t.Errorf("an open variant, its game not remembered: %+v", list)
	}
	if again := WithCopies(list, here); len(again) != 2 {
		t.Errorf("listed again: %+v", again)
	}
	if list := WithCopies([]Project{describe(entry{Path: gone})}, game); len(list) != 2 || list[1].Path != game {
		t.Errorf("a remembered copy's game: %+v", list)
	}
	if list := WithCopies(nil, game); len(list) != 0 {
		t.Errorf("a game's own folder isn't a copy: %+v", list)
	}
}

// In a repository, a variant's game is where the game sits in it: a variant of
// games/a, opened at its games/a, is a's; its games/b is not.
func TestCopyOfInARepository(t *testing.T) {
	repo := t.TempDir()
	if out, err := exec.Command("git", "-C", repo, "init", "-q").CombinedOutput(); err != nil {
		t.Skipf("git: %v %s", err, out)
	}
	a := filepath.Join(repo, "games", "a")
	v := filepath.Join(a, ".jm", "variants", "v")
	for _, d := range []string{a, filepath.Join(repo, "games", "b"), filepath.Join(v, "games", "a"), filepath.Join(v, "games", "b")} {
		os.MkdirAll(d, 0o755)
		os.WriteFile(filepath.Join(d, ".jm.json"), []byte(`{}`), 0o644)
	}
	if g, _, name := CopyOf(filepath.Join(v, "games", "a")); g != a || name != "v" {
		t.Errorf("a's variant: %q %q", g, name)
	}
	if g, _, _ := CopyOf(filepath.Join(v, "games", "b")); g != "" {
		t.Errorf("b in a's variant: %q", g)
	}
}
