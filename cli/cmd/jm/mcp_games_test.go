package main

import (
	"context"
	"encoding/json"
	"image/color"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// scratchSettings keeps the person's list of games (the editor's settings) out of a test.
func scratchSettings(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("HOME", dir)
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_DATA_HOME", dir)
}

func TestMCPMakesAndOpensGamesFromNowhere(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	wd, _ := os.Getwd()
	defer os.Chdir(wd)
	os.Chdir(t.TempDir()) // no game here, like Claude Desktop's server

	s := newMCPServer()
	call := func(name string, args map[string]any) toolResult {
		for _, tool := range s.tools {
			if tool.Name == name {
				return tool.run(args)
			}
		}
		t.Fatalf("no tool %s", name)
		return toolResult{}
	}
	if got := call("games", nil); !strings.Contains(got.Text, "No game open") || !strings.Contains(got.Text, "No games yet") {
		t.Errorf("games before: %s", got.Text)
	}
	if got := call("new_game", map[string]any{"name": "Neon Vow"}); got.Failed {
		t.Fatal(got.Text)
	}
	if want, _ := filepath.EvalSymlinks(filepath.Join(home, "Neon Vow")); s.opened() != want {
		t.Fatalf("new_game didn't open the new game: %q open", s.opened())
	}
	if got := call("new_game", map[string]any{"name": "Neon Vow"}); !got.Failed {
		t.Error("a second game of the same name must be refused")
	}
	for _, bad := range []string{"..", ".hidden"} {
		if got := call("new_game", map[string]any{"name": bad}); !got.Failed {
			t.Errorf("new_game %q escaped the games folder: %s", bad, got.Text)
		}
	}

	s.openGame(t.TempDir())
	if got := call("open_game", map[string]any{"game": "Neon Vow"}); got.Failed || filepath.Base(s.opened()) != "Neon Vow" {
		t.Fatalf("open_game by name: %s", got.Text)
	}
	if got := call("games", nil); !strings.Contains(got.Text, "Open: Neon Vow") || !strings.Contains(got.Text, `open_game "Neon Vow"`) {
		t.Errorf("games after: %s", got.Text)
	}
	if got := call("open_game", map[string]any{"game": "Nope"}); !got.Failed {
		t.Error("an unknown game must fail")
	}
}

func TestGamesListsWhatThePersonOpenedAnywhereNewestFirst(t *testing.T) {
	scratchSettings(t)
	root := t.TempDir()
	home := filepath.Join(root, "games")
	t.Setenv("JM_GAMES", home)
	wd, _ := os.Getwd()
	defer os.Chdir(wd)
	inFolder, err := newGame(context.Background(), home, newGameChoice{name: "In Folder"})
	if err != nil {
		t.Fatal(err)
	}
	elsewhere := filepath.Join(root, "work", "elsewhere")
	os.MkdirAll(elsewhere, 0o755)
	if err := runInit(elsewhere, "Elsewhere", io.Discard); err != nil {
		t.Fatal(err)
	}
	golden := filepath.Join(elsewhere, "tests", "golden", "title", "frame_00001.png")
	os.MkdirAll(filepath.Dir(golden), 0o755)
	writePNG(t, golden, 64, 36, color.RGBA{200, 40, 40, 255})

	s := newMCPServer()
	call := func(name string, args map[string]any) toolResult {
		for _, tool := range s.tools {
			if tool.Name == name {
				return tool.run(args)
			}
		}
		t.Fatalf("no tool %s", name)
		return toolResult{}
	}
	if got := call("open_game", map[string]any{"game": elsewhere}); got.Failed {
		t.Fatal(got.Text)
	}
	got := call("games", nil)
	games := got.Structured.(map[string]any)["games"].([]projects.Project)
	if len(games) != 2 || games[0].Name != "Elsewhere" || games[0].Opened == 0 || games[1].Name != "In Folder" || games[1].Opened != 0 {
		t.Fatalf("games: %+v", games)
	}
	if want := evalOr(elsewhere); got.Structured.(map[string]any)["open"] != want || games[0].Path != want {
		t.Errorf("open %v, first %s, want %s", got.Structured.(map[string]any)["open"], games[0].Path, want)
	}
	thumbs := got.Meta["jm/thumbs"].(map[string]string)
	if !strings.HasPrefix(thumbs[games[0].Path], "data:image/jpeg;base64,") || thumbs[evalOr(inFolder)] != "" {
		t.Errorf("thumbs: %v", thumbs)
	}
	if !strings.Contains(got.Text, `open_game "In Folder"`) || !strings.Contains(got.Text, `open_game "`+games[0].Path+`"`) {
		t.Errorf("games text: %s", got.Text)
	}

	// The project tools act on the game named, else the open one.
	skipOnWindows(t)
	args := filepath.Join(root, "editor-args")
	t.Setenv("CODEX_SANDBOX", "")
	t.Setenv("JM_EDITOR", fakeProgram(t, root, "editor", `echo "$@" > `+args+`.new; mv `+args+`.new `+args+`; sleep 3`))
	saved := startup
	startup = 200 * time.Millisecond
	defer func() { startup = saved }()
	for game, want := range map[string]string{"In Folder": evalOr(inFolder), "": games[0].Path} {
		if got := call("project_open_editor", map[string]any{"game": game}); got.Failed {
			t.Fatal(got.Text)
		}
		ran, _ := os.ReadFile(args)
		for start := time.Now(); len(ran) == 0 && time.Since(start) < 3*time.Second; ran, _ = os.ReadFile(args) {
			time.Sleep(20 * time.Millisecond)
		}
		os.Remove(args)
		if evalOr(strings.TrimSpace(string(ran))) != want {
			t.Errorf("project_open_editor %q opened %s", game, ran)
		}
	}
}

func evalOr(p string) string {
	if r, err := filepath.EvalSymlinks(p); err == nil {
		return r
	}
	return p
}

func TestGamesFolderStaysPutWhenTheOpenGameMoves(t *testing.T) {
	scratchSettings(t)
	wd, _ := os.Getwd()
	defer os.Chdir(wd)
	start := t.TempDir()
	os.Chdir(start)
	t.Setenv("JM_GAMES", "games") // relative: resolved once, at the start
	home := gamesHome()
	a, err := newGame(context.Background(), home, newGameChoice{name: "A"})
	if err != nil {
		t.Fatal(err)
	}
	os.Chdir(a)
	b, err := newGame(context.Background(), home, newGameChoice{name: "B"})
	if err != nil || filepath.Dir(b) != filepath.Dir(a) {
		t.Errorf("B landed in %s, not beside A (%v)", b, err)
	}
	// A's own build/ folder isn't the listed game "build".
	os.MkdirAll(filepath.Join(a, "build"), 0o755)
	os.WriteFile(filepath.Join(a, "build", ".jm.json"), []byte(`{"name":"built"}`), 0o644)
	if _, err := newGame(context.Background(), home, newGameChoice{name: "build"}); err != nil {
		t.Fatal(err)
	}
	if dir, _ := findGame(home, "build"); dir != filepath.Join(home, "build") {
		t.Errorf("open_game build opened %s", dir)
	}
}

func TestNewGameNeverRemovesAFolderItDidntMake(t *testing.T) {
	home := t.TempDir()
	mine := filepath.Join(home, "Taken")
	os.MkdirAll(mine, 0o755)
	os.WriteFile(filepath.Join(mine, "notes.txt"), []byte("keep"), 0o644)
	if _, err := newGame(context.Background(), home, newGameChoice{name: "Taken"}); err == nil {
		t.Error("an existing folder must be refused")
	}
	if !exists(filepath.Join(mine, "notes.txt")) {
		t.Error("deleted a folder it didn't make")
	}
}

func TestOpenGameTakesAListedNameOrAnAbsolutePath(t *testing.T) {
	scratchSettings(t)
	root := t.TempDir()
	home := filepath.Join(root, "games")
	a, err := newGame(context.Background(), home, newGameChoice{name: "A"})
	if err != nil {
		t.Fatal(err)
	}
	for _, dir := range []string{filepath.Join(a, "build"), filepath.Join(root, "Outside"), home} {
		os.MkdirAll(dir, 0o755)
		os.WriteFile(filepath.Join(dir, ".jm.json"), []byte(`{"name":"x"}`), 0o644)
	}
	for _, bad := range []string{"A/build", "../Outside", ".", ".."} {
		if dir, err := findGame(home, bad); err == nil {
			t.Errorf("open_game %q opened %s", bad, dir)
		}
	}
	for _, good := range []string{"A", a} {
		if dir, err := findGame(home, good); err != nil || dir != a {
			t.Errorf("open_game %q: %s, %v", good, dir, err)
		}
	}
}

// Every tool says which game it works on: onGame gives it the call's game,
// onDriven the driven run's, or it's listed here. A new tool that's on neither list fails this.
func TestEveryToolSaysWhichGameItWorksOn(t *testing.T) {
	noGame := map[string]bool{ // they choose a game, or need none
		"games": true, "games_find": true, "new_game": true, "open_game": true,
		"companions": true, "json_edit": true, "studio": true,
	}
	// Design debt: these still find their game themselves (their own "game", or the
	// working folder). Each moves to onGame (todos: Design debt, G2–G6), then off this list.
	ownWay := map[string]bool{}
	for _, tool := range newMCPServer().tools {
		listed := noGame[tool.Name] || ownWay[tool.Name]
		switch {
		case (tool.onGame != nil || tool.onDriven != nil) && listed:
			t.Errorf("%s works on the call's game now: take it off the list", tool.Name)
		case tool.onGame == nil && tool.onDriven == nil && !listed:
			t.Errorf("%s: give it onGame (it works on a game), or list it", tool.Name)
		}
	}
}

// A call that names a game works on it, whatever's open; one that names none
// works on the open game; a game that isn't one fails, never falls back.
func TestAToolWorksOnTheGameTheCallNames(t *testing.T) {
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"a", "b"} {
		filesGame(t, filepath.Join(home, g))
		os.WriteFile(filepath.Join(home, g, "which.txt"), []byte(g), 0o644)
	}
	t.Chdir(filepath.Join(home, "b"))
	s := newMCPServer()
	read := func(args toolArgs) toolResult {
		args["path"] = "which.txt"
		return toolOf(t, s, "file_read").run(args)
	}
	if got := read(toolArgs{"game": "a"}); got.Failed || !strings.Contains(got.Text, "a") {
		t.Errorf("named a: %+v", got)
	}
	if got := read(toolArgs{}); got.Failed || !strings.Contains(got.Text, "b") {
		t.Errorf("named none (b open): %+v", got)
	}
	for _, bad := range []any{"nope", 3} {
		if got := read(toolArgs{"game": bad}); !got.Failed {
			t.Errorf("named %v: worked on %q", bad, got.Text)
		}
	}
	props := func(name string) map[string]any {
		return toolOf(t, s, name).InputSchema["properties"].(map[string]any)
	}
	if _, ok := props("vcs_status")["game"]; !ok {
		t.Error("a tool on a game doesn't say it takes game")
	}
}

// The open game is the folder opened, not a link's name for it: retargeting
// the link later doesn't move it. A folder that stops being a game isn't open.
func TestTheOpenGameIsTheFolderOpened(t *testing.T) {
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"a", "b"} {
		filesGame(t, filepath.Join(home, g))
		os.WriteFile(filepath.Join(home, g, "which.txt"), []byte(g), 0o644)
	}
	link := filepath.Join(t.TempDir(), "current")
	if err := os.Symlink(filepath.Join(home, "a"), link); err != nil {
		t.Skip("no symlinks here")
	}
	t.Chdir(home)
	s := newMCPServer()
	s.openGame(link)
	os.Remove(link)
	os.Symlink(filepath.Join(home, "b"), link)
	read := func() toolResult { return toolOf(t, s, "file_read").run(toolArgs{"path": "which.txt"}) }
	if got := read(); got.Failed || !strings.Contains(got.Text, "a") {
		t.Errorf("after the link moved to b: %+v", got)
	}
	os.Remove(filepath.Join(home, "a", ".jm.json"))
	if got := read(); !got.Failed {
		t.Errorf("a game no more, still open: %+v", got)
	}
}

// A call naming its game keeps it across a form's round, whatever opens meanwhile.
func TestANamedGamesFormRoundSurvivesAnotherGameOpening(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"a", "b"} {
		filesGame(t, filepath.Join(home, g))
	}
	t.Chdir(filepath.Join(home, "a"))
	s := newMCPServer()
	named := map[string]any{"game": "a"}
	first := modernCall(t, s, 1, "project_export", named, chatGPTCaps, nil, "")["result"].(map[string]any)
	if first["resultType"] != "input_required" {
		t.Fatalf("no form: %v", first)
	}
	s.openGame(filepath.Join(home, "b"))
	declined := map[string]any{"form1": map[string]any{"action": "decline"}}
	done := modernCall(t, s, 2, "project_export", named, chatGPTCaps, declined, first["requestState"].(string))
	if done["error"] != nil || done["result"].(map[string]any)["resultType"] != "complete" {
		t.Errorf("its retry: %v", done)
	}
}

// A server started in a game that stops being one works on the game opened last.
func TestACallFallsBackToTheRecentGameWhenTheOpenOneIsGone(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"a", "b"} {
		filesGame(t, filepath.Join(home, g))
		os.WriteFile(filepath.Join(home, g, "which.txt"), []byte(g), 0o644)
	}
	if err := projects.Remember(filepath.Join(home, "b")); err != nil {
		t.Fatal(err)
	}
	t.Chdir(filepath.Join(home, "a"))
	s := newMCPServer()
	os.Remove(filepath.Join(home, "a", ".jm.json"))
	data, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": 1, "method": "tools/call",
		"params": map[string]any{"name": "file_read", "arguments": map[string]any{"path": "which.txt"}}})
	reply, _ := s.respond(data)
	if out, _ := json.Marshal(reply); !strings.Contains(string(out), `"text":"b`) {
		t.Errorf("read: %s", out)
	}
}

// A call from a file the host opened works on that file's game, whichever is open.
func TestAnOpenedFileNamesItsGame(t *testing.T) {
	fakeInspect(t) // which game's file: not how the engine lays it out
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"A", "B"} {
		filesGame(t, filepath.Join(home, g))
		os.WriteFile(filepath.Join(home, g, "level.scene.json"), []byte(`{"entities": [{"name": "From`+g+`"}]}`), 0o644)
	}
	s := newMCPServer()
	s.openGame(filepath.Join(home, "B"))
	opened := toolArgs{resourceKey: resourcePath(filepath.Join(home, "A", "level.scene.json"))}
	if got := toolOf(t, s, "scene_view").run(opened); got.Failed || !strings.Contains(got.Text, "FromA") {
		t.Errorf("A's scene, opened while B is: %s", got.Text)
	}
}

// Settings are the named game's, else the open game's, else only those beyond a game.
func TestSettingsAreTheCallsGameOrNone(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"A", "B"} {
		filesGame(t, filepath.Join(home, g))
		os.WriteFile(filepath.Join(home, g, ".jm.json"), []byte(`{"name": "`+g+`"}`), 0o644)
	}
	t.Chdir(t.TempDir())
	s := newMCPServer()
	name := func(a toolArgs) any {
		r := toolOf(t, s, "settings_read").run(a)
		if r.Failed {
			t.Fatalf("settings_read %v: %s", a, r.Text)
		}
		return r.Structured.(map[string]any)["values"].(map[string]any)["name"]
	}
	if got := name(toolArgs{}); got != nil {
		t.Errorf("no game open, yet a game's name: %v", got)
	}
	s.openGame(filepath.Join(home, "B"))
	if got := name(toolArgs{"game": "A"}); got != "A" {
		t.Errorf("named A while B is open: %v", got)
	}
	if r := toolOf(t, s, "settings_update").run(toolArgs{"game": "A", "set": map[string]any{"name": "A2"}}); r.Failed {
		t.Fatal(r.Text)
	}
	if got := name(toolArgs{}); got != "B" {
		t.Errorf("the open game's after A changed: %v", got)
	}
	if got := name(toolArgs{"game": "A"}); got != "A2" {
		t.Errorf("A after its change: %v", got)
	}
}

// An opened file names the game holding it, links resolved; one in no game names
// none (a call can't make a folder a game), and a call naming another game is refused.
func TestAnOpenedFileIsInItsGameOrNone(t *testing.T) {
	fakeInspect(t) // which game's file: not how the engine lays it out
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"A", "B"} {
		filesGame(t, filepath.Join(home, g))
		os.WriteFile(filepath.Join(home, g, "level.scene.json"), []byte(`{"entities": [{"name": "From`+g+`"}]}`), 0o644)
	}
	loose := t.TempDir()
	os.WriteFile(filepath.Join(loose, "loose.bindings.json"), []byte(`{}`), 0o644)
	link := filepath.Join(loose, "level.scene.json")
	if os.Symlink(filepath.Join(home, "A", "level.scene.json"), link) != nil {
		t.Skip("no symlinks here")
	}
	s := newMCPServer()
	s.openGame(filepath.Join(home, "B"))
	if got := toolOf(t, s, "scene_view").run(toolArgs{resourceKey: resourcePath(link)}); got.Failed || !strings.Contains(got.Text, "FromA") {
		t.Errorf("a link to A's scene: %s", got.Text)
	}
	looseFile := toolArgs{resourceKey: resourcePath(filepath.Join(loose, "loose.bindings.json")), "set": map[string]any{"name": "X"}}
	if got := toolOf(t, s, "settings_update").run(looseFile); !got.Failed || exists(filepath.Join(loose, ".jm.json")) {
		t.Errorf("a file in no game made its folder one: %s", got.Text)
	}
	named := toolArgs{resourceKey: resourcePath(filepath.Join(home, "A", "level.scene.json")), "game": "B"}
	if got := toolOf(t, s, "scene_view").run(named); !got.Failed {
		t.Errorf("A's file, called as B's: %s", got.Text)
	}
	// A page's own call carries its game too (Studio's tab): the file's, so it goes through.
	for _, game := range []string{"A", filepath.Join(home, "A"), filepath.Join(evalOrSelf(home), "A")} { // by name, path, real path
		agreed := toolArgs{resourceKey: resourcePath(link), "game": game}
		if got := toolOf(t, s, "scene_view").run(agreed); got.Failed || !strings.Contains(got.Text, "FromA") {
			t.Errorf("A's file, called as A's (%s): %s", game, got.Text)
		}
	}
}

// A picked link is the option offered, under its own name.
func TestAPickedLinkIsTheOptionOffered(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`, "source.json": `{}`})
	if os.Symlink("source.json", "level.scene.json") != nil {
		t.Skip("no symlinks here")
	}
	s := newMCPServer()
	abs := filepath.Join(evalOrSelf(here(t)), "level.scene.json") // as offered
	formHost(t, s, openaiForms, accept(map[string]any{"picked": fileURI(abs)}))
	got := toolOf(t, s, "pick").run(toolArgs{"kind": "scene"})
	if picked := got.Structured.(map[string]any)["picked"]; jsonOf(picked) != `["level.scene.json"]` {
		t.Errorf("picked: %v (%s)", picked, got.Text)
	}
}
