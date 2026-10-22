package main

import (
	"errors"
	"fmt"
	"net/url"
	"path/filepath"
	"runtime"
	"sync"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// The game a call works on (prepare): the game holding the file a host opened it from, else the one it names, else the open game. And the open game itself: what the server starts in, the client's roots, or the one opened last, until open_game or new_game.

// prepare readies a call: the game it runs in ("" for none) and how it runs.
// A tool on a game gets the game the call names, else the open one; it's
// resolved here, once, so the call's forms, log and work all agree on it.
func (s *mcpServer) prepare(tool mcpTool, args toolArgs) (game string, run func() toolResult) {
	failed := func(err error) func() toolResult { return func() toolResult { return textResult(err.Error(), true) } }
	switch {
	case tool.onGame != nil:
		root, err := s.gameRoot(args)
		if errors.Is(err, errNoGame) && tool.orNone {
			root, err = "", nil
		}
		if err != nil {
			return s.opened(), failed(err)
		}
		return root, func() toolResult { return tool.onGame(root, args) }
	case tool.onDriven != nil && s.game != nil:
		if v, named := args["game"]; named && v != "" {
			root, err := s.gameRoot(args)
			if err == nil && root != s.game.root {
				err = fmt.Errorf("the game driven is %s, not %s: drive_start {game} to drive that one", s.game.root, root)
			}
			if err != nil {
				return s.game.root, failed(err)
			}
		}
		return s.game.root, func() toolResult { return s.driving(args, tool.onDriven) }
	case tool.onDriven != nil:
		return s.opened(), func() toolResult { return tool.onDriven(args) } // nothing runs: it says so
	}
	return s.opened(), func() toolResult { return tool.run(args) }
}

// drivenArg is a driving tool's game: there's one driven game, so it only
// checks the call means it (an agent driving one game, another opened since).
var drivenArg = strArg("the game whose run this is (default: the one driven); another game's is refused")

// gameOfTool names the game a file or memory tool works on: a page showing one
// game says which, so another caller opening a game in between can't redirect it.
var gameOfTool = strArg("a name or path from `games` (default: the open game)")

// gameRoot is the game a call works on: the one holding the file a host opened
// it from, else the one its "game" names (a name or absolute path), else the open game.
func (s *mcpServer) gameRoot(a toolArgs) (string, error) {
	named := ""
	if v, given := a["game"]; given && v != "" {
		game, _ := v.(string) // mistyped: findGame says there's no such game
		root, err := findGame(s.home, game)
		if err != nil {
			return "", err
		}
		named = evalOrSelf(root)
	}
	if file := a.resource(); file != "" {
		root := gameHolding(file)
		switch {
		case root == "":
			return "", fmt.Errorf("%s isn't in a game: open_game its game first", filepath.Base(file))
		case named != "" && named != root:
			return "", fmt.Errorf("%s is %s's, not %s's", filepath.Base(file), gameName(root), gameName(named))
		}
		return root, nil
	}
	if named != "" {
		return named, nil
	}
	if root := s.opened(); root != "" {
		return root, nil
	}
	return "", errNoGame
}

var errNoGame = errors.New("No game open: games, then open_game.")

// gameHolding is the game folder holding file (its real path, links resolved: the
// nearest folder with a .jm.json), "" when no game does.
func gameHolding(file string) string {
	for dir := filepath.Dir(evalOrSelf(file)); ; dir = filepath.Dir(dir) {
		if exists(filepath.Join(dir, archive.ManifestEntryKey)) {
			return dir
		}
		if filepath.Dir(dir) == dir {
			return ""
		}
	}
}

// openGame makes dir the server's game, stopping the game the driver ran.
// openRecentIfNone: a server started outside a game (ChatGPT starts one per
// chat) works on the client's root that's a game, else the game opened last
// (the shared list), as if opened here.
func (s *mcpServer) openRecentIfNone() {
	if s.opened() != "" {
		return
	}
	if game := s.rootGame(); game != "" {
		s.openRootIfDefault(game)
		return
	}
	for _, p := range projects.List("") {
		if p.Opened > 0 && !p.Missing && s.setOpen(p.Path) == nil { // not Remember: the list stays as the person left it
			s.byRecent = true
			return
		}
	}
}

func (s *mcpServer) openGame(dir string) string {
	if err := s.setOpen(dir); err != nil {
		return err.Error()
	}
	s.byRecent = false
	_ = projects.Remember(dir) // the person's list of games: a convenience, so best effort
	return "Opened " + dir + ". Read its AGENTS.md first." + keepInMind(dir) + planned(dir)
}

// setOpen makes dir the game calls work on when they name none (the driven
// game is its own: it keeps running). It's kept as the folder's real path: a
// link retargeted later doesn't move it. The working folder never moves.
func (s *mcpServer) setOpen(dir string) error {
	real := evalOrSelf(dir)
	if !isDir(real) {
		return fmt.Errorf("no folder %s", dir)
	}
	s.open.Store(real)
	return nil
}

// opened is the open game: "" when none is, or its folder isn't a game any more.
func (s *mcpServer) opened() string {
	open, _ := s.open.Load().(string)
	if open == "" || !exists(filepath.Join(open, archive.ManifestEntryKey)) {
		return ""
	}
	return open
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

// rootsState is the client's roots, as the game to open when none is.
type rootsState struct {
	mu      sync.Mutex
	listed  bool          // the client has roots to ask for (its initialize said so)
	game    string        // its first root that's a game: "" when none is
	pending chan struct{} // closed once the roots last asked for are in (or never will be)
}

// rootsWait is how long the client may take to say its roots.
const rootsWait = 30 * time.Second

// askRoots asks the client its roots (when it has them), and opens the
// first that's a game if none is open by then. The answer comes back on the
// reader's goroutine, so this doesn't wait for it.
func (s *mcpServer) askRoots() {
	r := &s.roots
	r.mu.Lock()
	if s.peer == nil || !r.listed {
		r.mu.Unlock()
		return
	}
	r.mu.Unlock()
	// A new ask under the calls lock: an older answer checking it's the
	// latest, then opening its game (under the lock too), can't miss it.
	s.calls.Lock()
	done := make(chan struct{})
	r.mu.Lock()
	r.pending = done
	r.mu.Unlock()
	s.calls.Unlock()
	reply := s.peer.start("roots/list", map[string]any{})
	s.rootsWork.Add(1)
	go func() {
		defer s.rootsWork.Done()
		game := ""
		var got struct {
			Roots []struct {
				URI string `json:"uri"`
			} `json:"roots"`
		}
		select {
		case msg, ok := <-reply:
			if decode(msg, ok, &got) == nil {
				for _, root := range got.Roots {
					if game = gameAtRoot(root.URI); game != "" {
						break
					}
				}
			}
		case <-time.After(rootsWait):
			s.peer.abandon(reply)
		}
		r.mu.Lock()
		if r.pending == done { // else a newer ask's answer decides
			r.game = game
		}
		r.mu.Unlock()
		close(done)
		s.calls.Lock()
		defer s.calls.Unlock()
		r.mu.Lock()
		latest := r.pending == done // still: a newer ask may have gone out meanwhile
		r.mu.Unlock()
		if latest && game != "" {
			s.openRootIfDefault(game)
		}
	}()
}

// openRootIfDefault opens the root's game when no game is open, or the one
// open is only the last opened (openRecentIfNone, while the roots were slow
// to come): a game opened by name stays. The caller holds the calls lock.
func (s *mcpServer) openRootIfDefault(game string) {
	if s.opened() != "" && !s.byRecent {
		return
	}
	s.byRecent = false
	_ = s.setOpen(game)
}

// rootGame is the client's root that's a game, waiting a moment for roots
// asked for and not in yet: they'd say which game the person means.
func (s *mcpServer) rootGame() string {
	s.roots.mu.Lock()
	pending := s.roots.pending
	s.roots.mu.Unlock()
	if pending != nil {
		select {
		case <-pending:
		case <-time.After(2 * time.Second):
		}
	}
	s.roots.mu.Lock()
	defer s.roots.mu.Unlock()
	if s.roots.game != "" && exists(filepath.Join(s.roots.game, archive.ManifestEntryKey)) {
		return s.roots.game
	}
	return ""
}

// gameAtRoot is the folder a file:// root names when it's a game, else "".
func gameAtRoot(uri string) string {
	u, err := url.Parse(uri)
	if err != nil || u.Scheme != "file" || u.Path == "" || u.Host != "" && u.Host != "localhost" { // another machine's
		return ""
	}
	dir := filepath.FromSlash(u.Path)
	if runtime.GOOS == "windows" && len(u.Path) > 2 && u.Path[0] == '/' && u.Path[2] == ':' {
		dir = filepath.FromSlash(u.Path[1:]) // file:///C:/games/x
	}
	if !filepath.IsAbs(dir) || !exists(filepath.Join(dir, archive.ManifestEntryKey)) {
		return ""
	}
	return filepath.Clean(dir)
}
