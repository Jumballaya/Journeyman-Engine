package main

import (
	"crypto/rand"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// The game the person is playing now, in its own window (jm run, project_play):
// steered through its JM_CONTROL folder (engine/core/app/LiveControl.hpp).

// liveDir is where a project's played run takes requests.
func liveDir(root string) string { return filepath.Join(root, ".jm", "live") }

// liveStatus is a live run's status.json.
type liveStatus struct {
	Frame   uint64  `json:"frame"`
	Scene   string  `json:"scene"`
	Paused  bool    `json:"paused"`
	Speed   float64 `json:"speed"`
	Updated int64   `json:"updated"` // Unix seconds
}

// liveGame is the run playing root now; false when none is (it rewrites its status every second).
func liveGame(root string) (liveStatus, bool) {
	var s liveStatus
	data, err := os.ReadFile(filepath.Join(liveDir(root), "status.json"))
	if err != nil || json.Unmarshal(data, &s) != nil {
		return s, false
	}
	age := time.Since(time.Unix(s.Updated, 0))
	return s, age < 5*time.Second && age > -5*time.Second
}

// askLive runs one command in root's live run and gives its answer (a JSON object).
func askLive(root, command string) (map[string]any, error) {
	id := make([]byte, 8)
	_, _ = rand.Read(id)
	name := hex.EncodeToString(id)
	line, _ := json.Marshal(map[string]string{"id": name, "command": command})
	in, err := os.OpenFile(filepath.Join(liveDir(root), "in"), os.O_APPEND|os.O_WRONLY, 0)
	if err != nil {
		return nil, fmt.Errorf("the game isn't running: project_play starts it")
	}
	_, err = in.Write(append(line, '\n')) // one write: the game never takes half a line
	in.Close()
	if err != nil {
		return nil, err
	}
	answer := filepath.Join(liveDir(root), "answers", name+".json")
	for deadline := time.Now().Add(5 * time.Second); time.Now().Before(deadline); time.Sleep(15 * time.Millisecond) {
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
	return nil, fmt.Errorf("the game didn't answer %q within 5 s (is it stuck, or closed?)", command)
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
		}),
		run: func(a toolArgs) toolResult {
			root, err := projectRoot()
			if err != nil {
				return textResult(err.Error(), true)
			}
			if _, live := liveGame(root); !live {
				return textResult("The game isn't running: project_play opens it for the person to play.", true)
			}
			commands := a.list("commands")
			if len(commands) == 0 && a.str("command") != "" {
				commands = []string{a.str("command")}
			}
			var lines []string
			var answers []map[string]any
			for _, command := range commands {
				reply, err := askLive(root, command)
				if err != nil {
					return textResult(strings.Join(append(lines, err.Error()), "\n"), true)
				}
				answers = append(answers, reply)
				data, _ := json.Marshal(reply)
				lines = append(lines, string(data))
			}
			result := toolResult{Text: strings.Join(lines, "\n"), Structured: map[string]any{"answers": answers}}
			if a.flag("frame") {
				if jpeg, err := liveFrame(root); err != nil {
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

// liveFrame is the live game's last frame, as a JPEG.
func liveFrame(root string) ([]byte, error) {
	dir, err := os.MkdirTemp("", "jm-live-")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(dir)
	png := filepath.Join(dir, "frame.png")
	reply, err := askLive(root, "capture "+png)
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
