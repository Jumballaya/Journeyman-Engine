package main

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// An agent app may start jm mcp anywhere (Claude Desktop: no folder at all),
// so the server can make and open games itself. They live in the games folder:
// $JM_GAMES, else ~/Journeyman. The open game is the server's working folder.
func (s *mcpServer) gameTools() []mcpTool {
	home := gamesHome()
	return []mcpTool{
		{Name: "games", Description: "The game open now (if any) and the games in the games folder (" + home + "). Start here when you don't know which game the person means.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}),
			run: func(toolArgs) toolResult { return textResult(listGames(home), false) }},
		{Name: "new_game", Description: "Make a new game in the games folder (jm init) and open it: every other tool then works on it.",
			Annotations: writes(), InputSchema: object(map[string]any{"name": strArg("the game's name, e.g. \"Neon Vow\"")}, "name"),
			run: func(a toolArgs) toolResult {
				dir, err := newGame(home, a.str("name"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult(s.openGame(dir), false)
			}},
		{Name: "open_game", Description: "Open a game: a name from `games`, or the absolute path of a game's folder. Every other tool then works on it.",
			Annotations: writes(), InputSchema: object(map[string]any{"game": strArg("a name from `games`, or an absolute path to a folder with a .jm.json")}, "game"),
			run: func(a toolArgs) toolResult {
				dir, err := findGame(home, a.str("game"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return textResult(s.openGame(dir), false)
			}},
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

// findGame is the folder game names: an absolute path, else a game in the
// games folder (never a path from the open game).
func findGame(home, game string) (string, error) {
	dir := game
	if !filepath.IsAbs(game) {
		dir = filepath.Join(home, game)
	}
	if game == "" || !exists(filepath.Join(dir, archive.ManifestEntryKey)) {
		return "", fmt.Errorf("no game %q (games lists them)", game)
	}
	return filepath.Clean(dir), nil
}

func listGames(home string) string {
	var b strings.Builder
	if cwd, err := os.Getwd(); err == nil && exists(archive.ManifestEntryKey) {
		fmt.Fprintf(&b, "Open: %s (%s)\n", gameName(cwd), cwd)
	} else {
		b.WriteString("No game open: new_game or open_game.\n")
	}
	entries, _ := os.ReadDir(home)
	fmt.Fprintf(&b, "In %s:", home)
	found := 0
	for _, e := range entries {
		dir := filepath.Join(home, e.Name())
		if e.IsDir() && exists(filepath.Join(dir, archive.ManifestEntryKey)) {
			fmt.Fprintf(&b, "\n  %s (open_game %q)", gameName(dir), e.Name())
			found++
		}
	}
	if found == 0 {
		b.WriteString(" none yet")
	}
	return b.String()
}

func gameName(dir string) string {
	if man, err := manifest.LoadManifest(filepath.Join(dir, archive.ManifestEntryKey)); err == nil && man.Name != "" {
		return man.Name
	}
	return filepath.Base(dir)
}
