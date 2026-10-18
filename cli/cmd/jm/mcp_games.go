package main

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/examples"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// An agent app may start jm mcp anywhere (Claude Desktop: no folder at all),
// so the server can make and open games itself. They live in the games folder:
// $JM_GAMES, else ~/Journeyman. The open game is the server's working folder.
func (s *mcpServer) gameTools() []mcpTool {
	home := gamesHome()
	gameArg := strArg("a name or path from `games` (default: the open game)")
	return []mcpTool{
		{Name: "games", Title: "Journeyman", Meta: studio.toolMeta("Finding your games…", "Your games", entrypoint("global")), Description: "The game open now (if any) and the person's games: the ones they opened (here or in the editor), newest first, and the games folder's (" + home + "). " +
			"Start here when you don't know which game the person means.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}),
			run: func(toolArgs) toolResult { return s.listGames(home) }},
		{Name: "games_find", Title: "Find my games", Meta: callableMeta("Looking for your games…", "Found your games"),
			Description: "Look through the person's home folder (Documents, Desktop, Developer…, up to 4 folders down) for games and add them to their list: when `games` shows none, or not the one they mean.",
			Annotations: writes(), InputSchema: object(map[string]any{}),
			run: func(toolArgs) toolResult {
				userHome, err := os.UserHomeDir()
				if err != nil {
					return textResult(err.Error(), true)
				}
				added, err := projects.FindGames(userHome)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: fmt.Sprintf("Found %d new games.", len(added)), Structured: map[string]any{"added": added}}
			}},
		{Name: "new_game", Meta: callableMeta("Making the game…", "The game is made"), Description: "Make a new game in the games folder (jm init, or a copy of an example) and open it: every other tool then works on it. " +
			"Leave the name out to let the person choose in a form (what to start from, the name, the picture's shape), where the host shows forms.",
			Annotations: writes(), InputSchema: object(map[string]any{"name": strArg("the game's name, e.g. \"Neon Vow\""), "from": strArg("an example to start from (jm examples; default: an empty game)")}),
			run: func(a toolArgs) toolResult {
				choice := newGameChoice{name: a.str("name"), from: a.str("from")}
				if choice.name == "" {
					var outcome formOutcome
					switch choice, outcome = s.newGameForm(a.call().context(), home); outcome {
					case formUnavailable:
						return textResult("Give the game a name: new_game {name}.", true)
					case formDeclined, formPending:
						return textResult("The form closed without an answer (in Full access the host closes forms unseen): no game made. Ask the person in chat what to call it (and what to start from), then new_game {name, from}.", false)
					}
				}
				dir, err := newGame(a.call().context(), home, choice)
				if a.call().context().Err() != nil { // cancelled, the examples' download with it
					if err == nil {
						_ = os.RemoveAll(dir)
					}
					return textResult("cancelled: no game made", true)
				}
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: s.openGame(dir), Structured: map[string]any{"made": dir}}
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
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameArg,
				"fullscreen": boolArg("true: open it fullscreen, false: in a window, whatever the game and the person's saved setting say (default: as they say)")}),
			run: inGame(home, func(dir string, a toolArgs) toolResult {
				var how []string
				if on, ok := a["fullscreen"].(bool); ok {
					how = []string{map[bool]string{true: "--fullscreen", false: "--windowed"}[on]}
				}
				if err := playGame(a.call(), dir, how...); err != nil {
					return textResult(err.Error(), true)
				}
				_ = projects.Remember(dir)
				return textResult("The game is open for the person to play ("+dir+").", false)
			})},
		{Name: "project_export", Title: "Export a game", Meta: callableMeta("Exporting…", "Exported"), Description: "jm export on a game (default: the open game): build it and write a standalone executable (an .app on macOS) to its dist/. " +
			"Leave target out to let the person choose (platform, server, Mac signing) in a form, where the host shows forms.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": gameArg, "target": exportTargetArg}),
			run: inGame(home, func(dir string, a toolArgs) toolResult {
				args := exportArgs(a)
				if a.str("target") == "" && !a.flag("server") {
					form, outcome := s.exportForm(dir)
					switch outcome {
					case formDeclined, formPending:
						return textResult("The form closed without an answer (in Full access the host closes forms unseen): nothing exported. Ask the person in chat which platform, then project_export {target}.", false)
					case formAnswered:
						args = form
					}
				}
				return textResult(jmIn(a.call(), dir, args...))
			})},
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
// openRecentIfNone: a server started outside a game (ChatGPT starts one per
// chat) works on the game opened last (the shared list), as if opened here.
func (s *mcpServer) openRecentIfNone() {
	if exists(archive.ManifestEntryKey) {
		return
	}
	for _, p := range projects.List("") {
		if p.Opened > 0 && !p.Missing {
			s.stopDriver()       // whatever ran in the game that's gone
			_ = os.Chdir(p.Path) // not Remember: the list stays as the person left it
			return
		}
	}
}

func (s *mcpServer) openGame(dir string) string {
	if cwd, _ := os.Getwd(); evalOrSelf(cwd) != evalOrSelf(dir) {
		s.stopDriver() // another game's; opening the open one again keeps its game running
	}
	if err := os.Chdir(dir); err != nil {
		return err.Error()
	}
	_ = projects.Remember(dir) // the person's list of games: a convenience, so best effort
	return "Opened " + dir + ". Read its AGENTS.md first."
}

// newGame makes the game c names in home: an empty one (jm init) or a copy of
// an example, renamed, with c's settings.
func newGame(ctx context.Context, home string, c newGameChoice) (string, error) {
	name := strings.TrimSpace(c.name)
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
	if err := makeGame(ctx, dir, name, c); err != nil {
		os.RemoveAll(dir)
		return "", err
	}
	return dir, nil
}

func makeGame(ctx context.Context, dir, name string, c newGameChoice) error {
	if c.from == "" {
		if err := runInit(dir, name, io.Discard); err != nil {
			return err
		}
	} else if err := examples.Copy(ctx, version, c.from, dir, io.Discard); err != nil {
		return err
	}
	f, err := loadSettingsFile(filepath.Join(dir, archive.ManifestEntryKey))
	if err != nil {
		return err
	}
	f.setPath("name", name)
	for path, v := range c.settings {
		f.setPath(path, v)
	}
	return f.save()
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
func (s *mcpServer) listGames(home string) toolResult {
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
	return toolResult{Text: b.String(), Structured: map[string]any{"open": open, "games": list},
		Meta: map[string]any{"jm/thumbs": thumbs}}
}

func gameName(dir string) string {
	if man, err := manifest.LoadManifest(filepath.Join(dir, archive.ManifestEntryKey)); err == nil && man.Name != "" {
		return man.Name
	}
	return filepath.Base(dir)
}
