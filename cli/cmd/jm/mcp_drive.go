package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// The driven game: one run of a game the agent steps, reads and sees (drive_start … drive_stop).

func (s *mcpServer) driveTools() []mcpTool {
	return []mcpTool{
		{Name: "drive_start", Description: "For the person's own plays use play_show first. Start the built game under the stepped driver (headless; no window or GL unless gl is true). " +
			"It builds the game first when there is no build of its sources as they are (a file added, changed or gone). " +
			"It waits at frame 0 until told to step, or with play, at that moment of the person's recorded play. One game at a time; starting again restarts it. " +
			"With record, what you play is recorded as a play like the person's (drive_stop gives its id; play_show, play_frame and the timeline then work on it).",
			Annotations: writes(), InputSchema: object(map[string]any{
				"scene":   strArg("start in this scene instead of the entry scene"),
				"session": map[string]any{"type": "object", "description": "game state set before the first frame (a deep link)"},
				"gl":      boolArg("render with OpenGL, so drive_frame can show the game (needs a display)"),
				"play":    strArg("start at a moment of a recorded play instead (plays_list): replayed exactly up to there, then yours to drive"),
				"at":      strArg(`with play: the moment, e.g. "m2", "12.5s", a frame (default: its end)`),
				"record":  boolArg("record this run as a play (drive_stop gives its id)"),
				"seed":    intArg("the run's random seed (default: a new one; a play replays its own)"),
				"visible": boolArg("show the game in a window as you drive it, so the person can watch (implies gl)"),
			}),
			onGame: s.startDriver},
		{Name: "drive", Description: "Driver commands, each answered as JSON: step [n], state [part...] [tag=Name...] [Component...] (e.g. state session tag=Player), get [tag=Name] <path> (get tag=Ball TransformComponent.x), " +
			"down|up|press <Key>, move x y, click [x y], wheel dy, set <key> <json>, scene <path>, marker [note] (marks the moment in a recorded run, as F8 does), " +
			"until [tag=Name] <path> <op> <value> [max n] (steps until it's true, e.g. until tag=Lift TransformComponent.y < -270), echo <text>, " +
			"near tag=Name [distance] (near misses: what's within distance, with the gaps), " +
			"debug physics on|off (colliders and terrain drawn over the frame), profile (as drive_profile), capture (the frame now, as an image; needs gl), quit. " +
			"To follow something frame by frame, send commands with repeat instead of a call per frame: " +
			"commands [\"step 1\", \"get tag=Player TransformComponent.y\"], repeat 40 (a reply line each). " +
			"A failing command stops the batch: the commands before it already ran, and the result is an error holding the replies so far. " +
			"Past 1 MB of replies the batch stops too (the last line says {\"truncated\":true,\"after\":n commands}): ask for less, e.g. get instead of state.",
			Annotations: writes(), InputSchema: object(map[string]any{
				"command":  strArg(`e.g. "step 60", "press Enter", "state"`),
				"commands": listArg("several commands in order, instead of command"),
				"repeat":   intArg("run commands this many times over (default 1, at most 1000)"),
			}),
			onDriven: func(a toolArgs) toolResult {
				commands := a.list("commands")
				if len(commands) == 0 && a.str("command") != "" {
					commands = []string{a.str("command")}
				}
				if len(commands) == 0 {
					return textResult(`give a command, e.g. "step 60", or commands`, true)
				}
				return s.driveBatch(a.call(), commands, int(a.num("repeat", 1, 1, 1000)), driveReplyLimit)
			}},
		{Name: "drive_frame", Title: "See the driven game", Description: "An image of the driven game as it is now (the last frame it ran). " +
			"Needs a game started with gl: true. Use it to see what your commands did, e.g. after step 60 or a click.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}), onDriven: s.driveFrame},
		{Name: "drive_profile", Title: "Where the driven game's time goes", Description: "Call when the game feels slow or you changed something costly: " +
			"where the driven game's last frames (up to 120) went: each system's, script's and module's average and worst ms, " +
			"and counts (entities, scripts, draw calls, sprites). Step to the moment you care about first. " +
			"Without gl there's no GPU work, so the CPU's share is what it measures.",
			Annotations: readOnly(), InputSchema: object(map[string]any{}), onDriven: s.driveProfile},
		{Name: "drive_stop", Description: "Stop the driven game; a recorded run's play id (and its game) comes back.", Annotations: writes(),
			InputSchema: object(map[string]any{}),
			onDriven: func(toolArgs) toolResult {
				play, game := s.endRun()
				if play == "" {
					return textResult(`{"ok":true}`, false)
				}
				return jsonResult(map[string]any{"ok": true, "play": play, "game": game, "next": "play_show {play, game} shows it, as it does the person's plays"})
			}},
	}
}

// driving runs a tool on the driven game, which its call's cancel kills, mid-step or not.
func (s *mcpServer) driving(a toolArgs, run func(toolArgs) toolResult) toolResult {
	release := s.game.stopOn(a.call().context())
	result := run(a)
	if release(); a.call().context().Err() != nil {
		s.stopDriver() // killed (or about to be): reaped, its folder gone
	}
	return result
}

func (s *mcpServer) startDriver(root string, a toolArgs) toolResult {
	s.stopDriver()
	// The build is jm's, not the person's files: one that's missing or older
	// than the sources is made first, so the game driven is the game as it is.
	if err := freshBuild(a.call(), root); err != nil {
		return textResult(err.Error()+"\n(so there's no game to run)", true)
	}
	o := gameOptions{GL: a.flag("gl"), Visible: a.flag("visible"), Scene: a.str("scene"), Record: a.flag("record")}
	o.Session, _ = a["session"].(map[string]any)
	if seed, ok := a["seed"].(float64); ok {
		if seed < 0 || seed != float64(uint64(seed)) {
			return textResult("seed is a whole number, 0 or more", true)
		}
		o.Seed = new(uint64)
		*o.Seed = uint64(seed)
	}
	var from uint64 // with a play: the frame to drive on from
	if ref := a.str("play"); ref != "" {
		if o.Scene != "" || len(o.Session) > 0 || o.Seed != nil {
			return textResult("a play brings its own scene, session and seed: give play (and at), or those", true)
		}
		p, err := plays.Find(root, ref)
		if err == nil {
			from, err = p.FrameAt(a.str("at"))
		}
		if err != nil {
			return textResult(err.Error(), true)
		}
		o.Play, o.Until = p, from+1
	}
	o.Stop = a.call().context() // while it starts and replays: then it outlives the call
	var err error
	if s.game, err = startGame(root, o); err != nil {
		return textResult(err.Error(), true)
	}
	if o.Play != nil {
		// Replay the play up to the moment: the game is then where the person was.
		err = s.game.runTo(a.call(), from, 0, float64(from+1))
	}
	if s.game.unwatch(); err == nil {
		err = context.Cause(o.Stop) // cancelled as it got ready
	}
	if err != nil {
		s.stopDriver()
		return textResult(err.Error(), true)
	}
	return jsonResult(map[string]any{"ok": true, "ready": true, "frame": s.game.frame, "recording": o.Record})
}

// driveCommand sends one command to the driven game: its reply line, the
// frame's image for capture, and whether it failed (the game refused it, or ended).
func (s *mcpServer) driveCommand(command string) (string, *mcpImage, bool) {
	if s.game == nil {
		return "no game running: call drive_start first", nil, true
	}
	verb := strings.Fields(command)
	switch {
	case strings.ContainsAny(command, "\n\r"):
		return "one command per call", nil, true
	case len(verb) == 0 || strings.HasPrefix(verb[0], "#"):
		return `give a command, e.g. "step 60" (a blank or # line gets no answer)`, nil, true
	case verb[0] == "capture":
		// Into the driver's folder, whatever path it names: the engine writes wherever it's told.
		path, frame, err := s.captureFrame()
		if err != nil {
			return err.Error(), nil, true
		}
		data := jpegOf(path, 960)
		if data == nil {
			return "couldn't read the image " + path, nil, true
		}
		return fmt.Sprintf(`{"ok":true,"frame":%d,"image":"attached"}`, frame), &mcpImage{data, "image/jpeg"}, false
	}
	line, err := s.game.send(command)
	if err != nil {
		return s.ended(err.Error()), nil, true
	}
	if verb[0] == "quit" {
		return s.ended(line), nil, false
	}
	var reply struct {
		OK bool `json:"ok"`
	}
	_ = json.Unmarshal([]byte(line), &reply)
	return line, nil, !reply.OK
}

// driveReplyLimit keeps a batch's replies to what an agent's context can take.
const driveReplyLimit = 1 << 20

// driveBatch runs commands repeat times over, a reply line each. It stops at
// the first failure (the rest would run on from somewhere unexpected) and once
// the replies, images included, pass limit bytes, saying so in a last line.
func (s *mcpServer) driveBatch(call *toolCall, commands []string, repeat, limit int) toolResult {
	var replies []string
	var images []mcpImage
	result := func(failed bool) toolResult {
		return toolResult{Text: strings.Join(replies, "\n"), Images: images, Failed: failed}
	}
	size, ran := 0, 0
	for range repeat {
		for _, c := range commands {
			if size > limit {
				replies = append(replies, fmt.Sprintf(`{"truncated":true,"after":%d}`, ran))
				return result(false)
			}
			call.progress(float64(ran+1), float64(repeat*len(commands)), c)
			reply, img, failed := s.driveCommand(c)
			replies = append(replies, reply)
			size += len(reply) + 1
			if img != nil {
				images = append(images, *img)
				size += base64.StdEncoding.EncodedLen(len(img.Data))
			}
			ran++
			if failed {
				return result(true)
			}
		}
	}
	return result(false)
}

// ended stops a game that has ended, adding the play it recorded (and its game) to reply.
func (s *mcpServer) ended(reply string) string {
	if play, game := s.endRun(); play != "" {
		return reply + "\n" + fmt.Sprintf(`{"play":%q,"game":%q,"next":"play_show {play, game} shows it"}`, play, game)
	}
	return reply
}

// endRun stops the driven game: the play it recorded ("" for none) and its game.
func (s *mcpServer) endRun() (play, game string) {
	if s.game != nil {
		game = s.game.root
	}
	return s.stopDriver(), game
}

// stopDriver ends the driven game, if one runs: the id of the play it
// recorded, "" when it didn't.
func (s *mcpServer) stopDriver() string {
	if s.game == nil {
		return ""
	}
	play := s.game.close()
	s.game = nil
	return play
}

// driveFrame is an image of the driven game as it is now (the last frame run):
// the agent gets the image, not a file to manage.
func (s *mcpServer) driveFrame(toolArgs) toolResult {
	if s.game == nil {
		return textResult("no game running: call drive_start first (with gl: true, to see it)", true)
	}
	path, frame, err := s.captureFrame()
	if err != nil {
		return textResult(err.Error(), true)
	}
	return imageResult(path, fmt.Sprintf("Frame %d of the driven game.", frame), map[string]any{"frame": frame})
}

// captureFrame saves the driven game's last frame run: its image file and number.
func (s *mcpServer) captureFrame() (string, uint64, error) {
	path, err := s.game.capture()
	if err != nil && strings.Contains(err.Error(), "JM_RENDERER=none") {
		err = fmt.Errorf("this game was started without GL: drive_start with gl: true to see it (state has the draw list without)")
	}
	return path, s.game.frame - min(s.game.frame, 1), err
}

func (s *mcpServer) driveProfile(toolArgs) toolResult {
	if s.game == nil {
		return textResult("no game running: call drive_start first, then step", true)
	}
	reply, err := s.game.do("profile")
	if err != nil {
		return textResult(err.Error(), true)
	}
	var profile map[string]any
	if err := json.Unmarshal(reply["profile"], &profile); err != nil {
		return textResult(err.Error(), true)
	}
	return jsonResult(profile)
}
