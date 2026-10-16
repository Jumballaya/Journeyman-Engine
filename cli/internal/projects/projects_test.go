package projects

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
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
