package main

import (
	_ "embed"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

//go:embed widget/hub.html
var hubPage string

// hub is the person's games as a page: a host's sidebar opens it (ChatGPT).
var hub = widget{name: "journeyman-hub", title: "Journeyman", html: hubPage, modes: []string{"fullscreen", "inline"},
	description: "The person's games: play one, open it in the editor or here, export it, or make a new one."}

// An agent app may start jm mcp anywhere (Claude Desktop: no folder at all),
// so the server can make and open games itself. They live in the games folder:
// $JM_GAMES, else ~/Journeyman. The open game is the server's working folder.
func (s *mcpServer) gameTools() []mcpTool {
	home := gamesHome()
	gameArg := strArg("a name or path from `games` (default: the open game)")
	return []mcpTool{
		{Name: "games", Title: "Journeyman", Meta: hub.toolMeta("Finding your games…", "Your games", entrypoint("global")), Description: "The game open now (if any) and the person's games: the ones they opened (here or in the editor), newest first, and the games folder's (" + home + "). " +
			"Start here when you don't know which game the person means.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}),
			run: func(toolArgs) toolResult { return listGames(home) }},
		{Name: "new_game", Meta: callableMeta("Making the game…", "The game is made"), Description: "Make a new game in the games folder (jm init) and open it: every other tool then works on it.",
			Annotations: writes(), InputSchema: object(map[string]any{"name": strArg("the game's name, e.g. \"Neon Vow\"")}, "name"),
			run: func(a toolArgs) toolResult {
				dir, err := newGame(home, a.str("name"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult(s.openGame(dir), false)
			}},
		{Name: "open_game", Meta: callableMeta("", ""), Description: "Open a game: a name or path from `games`, or the absolute path of a game's folder. Every other tool then works on it.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": strArg("a name or path from `games`, or an absolute path to a folder with a .jm.json")}, "game"),
			run: func(a toolArgs) toolResult {
				dir, err := findGame(home, a.str("game"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult(s.openGame(dir), false)
			}},
		{Name: "project_open_editor", Title: "Open a game in the editor", Meta: callableMeta("Opening the editor…", "The editor is open"), Description: "Open a game in the Journeyman editor on the person's screen (jm editor). The open game stays open here.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameArg}),
			run: inGame(home, func(dir string, _ toolArgs) toolResult {
				editor, err := findEditor()
				if err == nil {
					err = startEditor(editor, dir)
				}
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult("The editor is open on "+dir+".", false)
			})},
		{Name: "project_play", Title: "Play a game", Meta: callableMeta("Starting the game…", "The game is open"), Description: "Open a game in its own window for the person to play (jm run, built first if it needs it); the call doesn't wait. " +
			"The play is recorded: open_game it, then play_show.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameArg}),
			run: inGame(home, func(dir string, _ toolArgs) toolResult {
				if buildIsStale(dir) {
					if out, failed := jmIn(dir, "build", "--json"); failed {
						return textResult("The build failed:\n"+out, true)
					}
				}
				run := jmCommand(dir, "run")
				if err := startDetached(run, "the game"); err != nil {
					return textResult(err.Error(), true)
				}
				_ = projects.Remember(dir)
				return textResult("The game is open for the person to play ("+dir+").", false)
			})},
		{Name: "project_export", Title: "Export a game", Meta: callableMeta("Exporting…", "Exported"), Description: "jm export on a game (default: the open game): build it and write a standalone executable (an .app on macOS) to its dist/.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameArg, "target": exportTargetArg}),
			run: inGame(home, func(dir string, a toolArgs) toolResult { return textResult(jmIn(dir, exportArgs(a)...)) })},
	}
}

// inGame runs a tool on the game its "game" argument names, else the open one.
func inGame(home string, run func(dir string, a toolArgs) toolResult) func(toolArgs) toolResult {
	return func(a toolArgs) toolResult {
		game := a.str("game")
		if game == "" {
			if !exists(archive.ManifestEntryKey) {
				return textResult("No game open: name one from games.", true)
			}
			game, _ = os.Getwd()
		}
		dir, err := findGame(home, game)
		if err != nil {
			return textResult(err.Error(), true)
		}
		return run(dir, a)
	}
}

// gamesHome is absolute: the server's working folder moves with the open game.
func gamesHome() string {
	dir := os.Getenv("JM_GAMES")
	if dir == "" {
		dir = filepath.Join(homeDir(), "Journeyman")
	}
	abs, _ := filepath.Abs(dir)
	return abs
}

// openGame makes dir the server's game, stopping the game the driver ran.
func (s *mcpServer) openGame(dir string) string {
	s.stopDriver()
	if err := os.Chdir(dir); err != nil {
		return err.Error()
	}
	_ = projects.Remember(dir) // the person's list of games: a convenience, so best effort
	return "Opened " + dir + ". Read its AGENTS.md first."
}

func newGame(home, name string) (string, error) {
	folder := exportName(name)
	if strings.HasPrefix(folder, ".") || filepath.Base(folder) != folder {
		return "", fmt.Errorf("%q can't be a folder name: pick another name", name)
	}
	dir := filepath.Join(home, folder)
	if err := os.MkdirAll(home, 0o755); err != nil {
		return "", err
	}
	// Mkdir, not MkdirAll: of two servers making the same game, one gets an error.
	if err := os.Mkdir(dir, 0o755); os.IsExist(err) {
		return "", fmt.Errorf("%s already exists: open_game %q, or pick another name", dir, folder)
	} else if err != nil {
		return "", err
	}
	if err := runInit(dir, strings.TrimSpace(name), io.Discard); err != nil {
		os.RemoveAll(dir)
		return "", err
	}
	return dir, nil
}

// findGame is the folder game names: an absolute path, else one folder in the
// games folder (never a path from the open game, nor one nested or beside it).
func findGame(home, game string) (string, error) {
	dir := game
	if !filepath.IsAbs(game) {
		dir = filepath.Join(home, game)
		if filepath.Base(game) != game || filepath.Dir(dir) != home {
			return "", fmt.Errorf("no game %q: give a name from games, or an absolute path", game)
		}
	}
	if game == "" || !exists(filepath.Join(dir, archive.ManifestEntryKey)) {
		return "", fmt.Errorf("no game %q (games lists them)", game)
	}
	return filepath.Clean(dir), nil
}

// listGames is the game open now and the person's games; their pictures go
// to a widget only (_meta), as small JPEGs.
func listGames(home string) toolResult {
	var b strings.Builder
	open := ""
	if cwd, err := os.Getwd(); err == nil && exists(archive.ManifestEntryKey) {
		open = evalOrSelf(cwd)
		fmt.Fprintf(&b, "Open: %s (%s)\n", gameName(cwd), open)
	} else {
		b.WriteString("No game open: new_game or open_game.\n")
	}
	list := projects.List(home)
	if list == nil {
		list = []projects.Project{} // "games": [], not null
	}
	b.WriteString(projectsText(list, home))
	thumbs := map[string]string{}
	for _, p := range list {
		if uri := dataURI(p.Thumb, 320); uri != "" {
			thumbs[p.Path] = uri
		}
	}
	return toolResult{Text: b.String(), Structured: map[string]any{"open": open, "games": list}, Meta: map[string]any{"jm/thumbs": thumbs}}
}

func gameName(dir string) string {
	if man, err := manifest.LoadManifest(filepath.Join(dir, archive.ManifestEntryKey)); err == nil && man.Name != "" {
		return man.Name
	}
	return filepath.Base(dir)
}
