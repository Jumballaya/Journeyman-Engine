package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// gameOptions is how to run a driven game.
type gameOptions struct {
	Play    *plays.Play    // replay it (from its own save) ...
	Until   uint64         // ... through this many frames, then stop or take commands (0: all of it)
	GL      bool           // draw, in a hidden window: images can be captured
	Visible bool           // draw in a window the person can watch
	Seed    *uint64        // the run's randomness (default: a new seed)
	Scene   string         // start here instead of the entry scene
	Session map[string]any // game state set before the first frame
	Record  bool           // keep the run as a new play
}

// replyTimeout is how long a command may take (a long step, a slow capture)
// before the game is taken to be stuck.
var replyTimeout = 5 * time.Minute

// drivenGame is the project's build running under the engine's stepped driver
// (JM_DRIVE): a command in, a JSON line out, and nothing moves in between.
// Replays, `jm plays` and the MCP driver all run the game this way.
type drivenGame struct {
	cmd      *exec.Cmd
	in       io.WriteCloser
	out      *bufio.Scanner
	frame    uint64            // frames run so far
	errors   []json.RawMessage // what the game logged on the way
	work     string            // its save and captures, removed when it closes
	play     string            // the play it's recording, if it is
	captures int
}

// startGame runs the build in root/build as o says, ready at frame 0.
func startGame(root string, o gameOptions) (g *drivenGame, err error) {
	build := filepath.Join(root, "build")
	if _, err := os.Stat(filepath.Join(build, archive.ManifestEntryKey)); err != nil {
		return nil, errors.New("no build to run: jm build first")
	}
	engine, err := resolveEnginePath()
	if err == nil {
		engine, err = filepath.Abs(engine)
	}
	if err != nil {
		return nil, err
	}
	g = &drivenGame{}
	if g.work, err = os.MkdirTemp("", "jm-drive-"); err != nil {
		return nil, err
	}
	defer func() {
		if err != nil { // nothing of a game that didn't start stays behind
			_ = os.RemoveAll(g.work)
			if g.play != "" {
				_ = os.RemoveAll(g.play)
			}
		}
	}()
	env := append(os.Environ(), "JM_DRIVE=1")
	if o.Play != nil {
		// A replay starts from a copy of the player's save (the engine makes it).
		env = append(env, "JM_PLAY_SESSION="+o.Play.Dir)
		if o.Until > 0 {
			env = append(env, fmt.Sprintf("JM_PLAY_UNTIL=%d", o.Until))
		}
	} else {
		env = append(env, "JM_SAVE_DIR="+filepath.Join(g.work, "save"))
	}
	if !o.Visible {
		env = append(env, "JM_HEADLESS=1")
		if !o.GL {
			env = append(env, "JM_RENDERER=none")
		}
	}
	if o.Seed != nil {
		env = append(env, fmt.Sprintf("JM_SEED=%d", *o.Seed))
	}
	if o.Scene != "" {
		env = append(env, "JM_ENTRY_SCENE="+o.Scene)
	}
	if len(o.Session) > 0 {
		data, _ := json.Marshal(o.Session)
		path := filepath.Join(g.work, "session.json")
		if err := os.WriteFile(path, data, 0o644); err != nil {
			return nil, err
		}
		env = append(env, "JM_SESSION="+path)
	}
	if o.Record {
		_, _ = plays.Prune(root, keptPlays, true)
		if g.play, err = plays.Create(root, plays.ReadBuild(build), version); err != nil {
			return nil, err
		}
		env = append(env, "JM_RECORD_DIR="+g.play)
	}
	g.cmd = exec.Command(engine, ".")
	g.cmd.Dir, g.cmd.Env = build, env
	if g.in, err = g.cmd.StdinPipe(); err != nil {
		return nil, err
	}
	stdout, err := g.cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	g.out = bufio.NewScanner(stdout)
	g.out.Buffer(make([]byte, 1<<20), 1<<28)
	if err := g.cmd.Start(); err != nil {
		return nil, err
	}
	if _, err := g.send(""); err != nil { // its ready line
		g.close()
		return nil, err
	}
	return g, nil
}

// send gives the game a command ("" only reads) and returns its reply line.
func (g *drivenGame) send(command string) (string, error) {
	if command != "" {
		if _, err := fmt.Fprintln(g.in, command); err != nil {
			return "", errors.New("the game ended")
		}
	}
	answered := make(chan bool, 1)
	go func() { answered <- g.out.Scan() }()
	select {
	case ok := <-answered:
		if !ok {
			return "", errors.New("the game ended (its log: build/logs/engine.log)")
		}
	case <-time.After(replyTimeout):
		// Its reply won't come: the game is stopped (the read ends with it) and dropped.
		if g.cmd.Process != nil {
			_ = g.cmd.Process.Kill()
		}
		return "", fmt.Errorf("the game didn't answer %q in %s, so it was stopped", command, replyTimeout)
	}
	line := g.out.Text()
	var reply struct {
		Frame  *uint64           `json:"frame"`
		Errors []json.RawMessage `json:"errors"`
	}
	if json.Unmarshal([]byte(line), &reply) == nil {
		g.errors = append(g.errors, reply.Errors...)
		if reply.Frame != nil {
			g.frame = *reply.Frame
		}
	}
	return line, nil
}

// do sends a command and parses the reply, an error when the game refused it.
func (g *drivenGame) do(command string) (map[string]json.RawMessage, error) {
	line, err := g.send(command)
	if err != nil {
		return nil, err
	}
	var reply map[string]json.RawMessage
	if err := json.Unmarshal([]byte(line), &reply); err != nil {
		return nil, fmt.Errorf("the engine said %q", line)
	}
	if string(reply["ok"]) != "true" {
		var msg string
		_ = json.Unmarshal(reply["error"], &msg)
		return reply, fmt.Errorf("%s: %s", command, msg)
	}
	return reply, nil
}

// to runs the game through frame f: what it shows and holds is frame f's.
func (g *drivenGame) to(f uint64) error {
	if f < g.frame {
		return nil
	}
	_, err := g.do(fmt.Sprintf("step %d", f+1-g.frame))
	return err
}

// capture is an image of the last frame run, in the game's own folder.
func (g *drivenGame) capture() (string, error) {
	g.captures++
	path := filepath.Join(g.work, fmt.Sprintf("frame-%d.png", g.captures))
	_, err := g.do("capture " + path)
	return path, err
}

// close ends the game (killing one that doesn't stop), removes its folder and
// gives the id of the play it recorded, "" when it didn't.
func (g *drivenGame) close() string {
	_, _ = fmt.Fprintln(g.in, "quit")
	_ = g.in.Close()
	done := make(chan struct{})
	go func() { _ = g.cmd.Wait(); close(done) }()
	select {
	case <-done:
	case <-time.After(5 * time.Second):
		_ = g.cmd.Process.Kill()
		<-done
	}
	_ = os.RemoveAll(g.work)
	if g.play == "" {
		return ""
	}
	return filepath.Base(g.play)
}
