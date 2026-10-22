package main

import (
	"context"
	"crypto/rand"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// The game the person is playing now, in its own window (jm run, project_play):
// steered through its JM_CONTROL folder (engine/core/app/agent/LiveControl.hpp).

// liveDir holds the folders of a project's played runs, one each.
func liveDir(root string) string { return filepath.Join(root, ".jm", "live") }

// newLiveDir is a folder for a new played run, those of runs gone removed.
func newLiveDir(root string) string {
	dirs, _ := filepath.Glob(filepath.Join(liveDir(root), "*"))
	for _, dir := range dirs {
		if _, live := readLive(dir); !live {
			os.RemoveAll(dir)
		}
	}
	return filepath.Join(liveDir(root), strconv.Itoa(os.Getpid()))
}

// liveStatus is a live run's status.json.
type liveStatus struct {
	Frame   uint64  `json:"frame"`
	Scene   string  `json:"scene"`
	Paused  bool    `json:"paused"`
	Speed   float64 `json:"speed"`
	Updated int64   `json:"updated"` // Unix seconds
	Started int64   `json:"started"` // Unix ms: which run it is (updated only says it's live)
}

// readLive is the run steered through dir; false when it isn't running (it rewrites its status every second).
func readLive(dir string) (liveStatus, bool) {
	var s liveStatus
	data, err := os.ReadFile(filepath.Join(dir, "status.json"))
	if err != nil || json.Unmarshal(data, &s) != nil {
		return s, false
	}
	age := time.Since(time.Unix(s.Updated, 0))
	return s, age < 5*time.Second && age > -5*time.Second
}

// liveGame is the folder of the run playing root now (the latest started; run:
// that one, if it's live), "" for none.
func liveGame(root, run string) string {
	if run != "" {
		dir := filepath.Join(liveDir(root), filepath.Base(run))
		if _, live := readLive(dir); live {
			return dir
		}
		return ""
	}
	dirs, _ := filepath.Glob(filepath.Join(liveDir(root), "*"))
	var best string
	var bestStarted int64 = -1
	for _, dir := range dirs {
		if s, live := readLive(dir); live && s.Started > bestStarted {
			best, bestStarted = dir, s.Started
		}
	}
	return best
}

// askLive runs one command in the run steered through dir and gives its answer
// (a JSON object). Steps take their time: 5 s, and 50 ms a frame.
func askLive(ctx context.Context, dir, command string) (map[string]any, error) {
	id := make([]byte, 8)
	_, _ = rand.Read(id)
	name := hex.EncodeToString(id)
	line, _ := json.Marshal(map[string]string{"id": name, "command": command})
	in, err := os.OpenFile(filepath.Join(dir, "in"), os.O_APPEND|os.O_WRONLY, 0)
	if err != nil {
		return nil, fmt.Errorf("the game isn't running: project_play starts it")
	}
	_, err = in.Write(append(line, '\n')) // one write: the game never takes half a line
	in.Close()
	if err != nil {
		return nil, err
	}
	wait := 5 * time.Second
	if verb, n, _ := strings.Cut(command, " "); verb == "step" {
		frames, _ := strconv.Atoi(strings.TrimSpace(n))
		wait += time.Duration(min(frames, 100000)) * 50 * time.Millisecond
	}
	answer := filepath.Join(dir, "answers", name+".json")
	for deadline := time.Now().Add(wait); time.Now().Before(deadline) && ctx.Err() == nil; time.Sleep(15 * time.Millisecond) {
		data, err := os.ReadFile(answer)
		if err != nil {
			continue
		}
		os.Remove(answer)
		var reply map[string]any
		if err := json.Unmarshal(data, &reply); err != nil {
			return nil, err
		}
		return reply, nil
	}
	if ctx.Err() != nil {
		return nil, ctx.Err()
	}
	return nil, fmt.Errorf("the game didn't answer %q within %v (is it stuck, or closed?)", command, wait)
}

func (s *mcpServer) liveTools() []mcpTool {
	return []mcpTool{{Name: "play_live", Title: "The game the person is playing",
		Description: "Steer the game the person is playing right now in its own window (project_play, jm run): " +
			"pause, resume, step [n] (one frame at a time while paused), speed <0.1..1> (slow motion), status, " +
			"and the driver's commands: state, get <path> (watch a value), set <key> <json> (a session value), marker [note] (marks the moment in the recorded play), " +
			"profile (where frames' time goes). frame: true adds a picture of the game as it is. " +
			"Use it to look at the moment the person describes; resume what you paused.",
		Annotations: writes(), Meta: callableMeta("", ""),
		InputSchema: object(map[string]any{
			"command":  strArg(`e.g. "pause", "get session.score", "marker the jump feels floaty"`),
			"commands": listArg("several commands in order, instead of command"),
			"frame":    boolArg("also a picture of the game as it is now"),
			"run":      strArg("the run to steer, as an earlier answer named it (default: the latest started)"),
		}),
		onGame: func(root string, a toolArgs) toolResult {
			dir := liveGame(root, a.str("run"))
			if dir == "" {
				return textResult("The game isn't running: project_play opens it for the person to play.", true)
			}
			commands := a.list("commands")
			if len(commands) == 0 && a.str("command") != "" {
				commands = []string{a.str("command")}
			}
			var lines []string
			var answers []map[string]any
			for _, command := range commands {
				reply, err := askLive(a.call().context(), dir, command)
				if err != nil {
					return textResult(strings.Join(append(lines, err.Error()), "\n"), true)
				}
				answers = append(answers, reply)
				data, _ := json.Marshal(reply)
				lines = append(lines, string(data))
			}
			// Whose answers: a page following one game's run can tell another's.
			result := toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"answers": answers, "game": root, "run": filepath.Base(dir)}}
			if a.flag("frame") {
				if jpeg, err := liveFrame(a.call().context(), dir); err != nil {
					result.Text = strings.TrimSpace(result.Text + "\nNo picture: " + err.Error())
				} else {
					result.Images = []mcpImage{{Data: jpeg, MimeType: "image/jpeg"}}
					result.Meta = map[string]any{"jm/image": "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(jpeg)}
				}
			}
			if result.Text == "" {
				result.Text = "The game is running."
			}
			return result
		}}}
}

// liveFrame is the last frame of the run steered through live, as a JPEG.
func liveFrame(ctx context.Context, live string) ([]byte, error) {
	dir, err := os.MkdirTemp("", "jm-live-")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(dir)
	png := filepath.Join(dir, "frame.png")
	reply, err := askLive(ctx, live, "capture "+png)
	if err != nil {
		return nil, err
	}
	if reply["ok"] != true {
		return nil, fmt.Errorf("%v", reply["error"])
	}
	if jpeg := jpegOf(png, 960); jpeg != nil {
		return jpeg, nil
	}
	return nil, fmt.Errorf("the game wrote no picture")
}
