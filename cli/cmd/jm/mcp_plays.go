package main

import (
	"bytes"
	"cmp"
	_ "embed"
	"encoding/base64"
	"fmt"
	"image"
	"image/draw"
	"image/jpeg"
	_ "image/png"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
	"github.com/Jumballaya/Journeyman-Engine/internal/realpath"
)

// The recorded plays over MCP: the person plays (jm run records), and the
// agent looks at what they saw, at the moment they mean. play_show renders a
// timeline (widget/plays.html) the person can scrub, ask about and play on from.

//go:embed widget/plays.html
var playsPage string

var timeline = widget{name: "journeyman-plays", title: "Play timeline", html: playsPage, modes: []string{"inline", "fullscreen"},
	description: "A recorded play: scrub its moments, see what the player saw, ask about one or play on from it."}

// playRefArg and momentArgText say how a tool names one of the person's plays and a moment in it.
var playRefArg = strArg(`which play: an id, a unique start of one, "latest" (default: the person's newest), or "latest-1", "latest-2" for earlier ones; your own runs go by id`)

const momentArgText = `a moment: a frame ("420"), a time ("12.5s", "1:05"), a marker ("m2" or "marker:2"), `

func (s *mcpServer) playTools() []mcpTool {
	atArg, fromArg := strArg(momentArgText+`"start" or "end" (default)`), strArg(momentArgText+`"start" (default) or "end"`)
	return []mcpTool{
		{Name: "plays_list", Title: "List recorded plays",
			Description: "Every play the person recorded, newest first: id, length, when each marker is, and whether an older build made it. " +
				"For the latest play, call play_show instead (it shows the person the timeline).",
			InputSchema: object(map[string]any{}), Annotations: readOnly(), Meta: callableMeta("", ""),
			onGame: func(root string, _ toolArgs) toolResult {
				listing, err := listPlays(root)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(struct {
					Game string `json:"game"` // whose plays: a page asking for one game's can tell
					playListings
				}{evalOrSelf(root), listing})
			}},
		{Name: "play_show", Title: "Play review",
			Description: "Call first whenever the person talks about playing the game: how it felt, a bug, a death, a marker, \"just now\", " +
				"or how the game is going. Shows the person their play as a timeline they can scrub (latest play by default), " +
				"and gives you what happened: scenes over time, values that changed (lives, score, ...), and the moments they marked with F8 (what they meant is in what they tell you). " +
				"Then play_frame / play_state at a marker. After a change, at opens it at their moment and replays it with the change beside what they saw: show them, don't just tell them.",
			InputSchema: object(map[string]any{"play": playRefArg, "at": strArg(momentArgText + `"start" or "end": open the timeline there (default: its first marker)`)}),
			Annotations: readOnly(),
			Meta:        timeline.toolMeta("Opening the play…", "The play", entrypoint("thread")),
			onGame:      playShow},
		{Name: "play_frame", Title: "See a moment of a play",
			Description: "An image of a moment in a recorded play, replayed exactly (or the nearest thumbnail where nothing can draw: source says which). " +
				"Use it to see what the person saw, e.g. at: \"m1\" for their first marker.",
			InputSchema: object(map[string]any{"play": playRefArg, "at": atArg}), Annotations: readOnly(),
			Meta:   callableMeta("Replaying to that moment…", "That moment"),
			onGame: playFrame},
		{Name: "play_moment", Title: "What the person pointed at",
			Description: "A marker the person saved with F8 after picking entities, points or areas (a moment): the frame as they saw it with each pick outlined and numbered, " +
				"and the picks: each entity's id, tags, components then and origin, where it came from: a scene's entry (scene, entry, name, prefab) " +
				"or a spawn (the prefab, the frame, the entity and script that spawned it). A point or an area (empty space: \"a coin should be here\", \"this section is too dark\") " +
				"has its world coordinates (y up), the entities there with their origins, and the tile map cells it covers (map, layer, tile coordinates from the bottom row, gids). " +
				"What they picked is what they meant; origin, or the map, is the file to change. " +
				"play_show lists every marker's picks.",
			InputSchema: object(map[string]any{"play": playRefArg, "marker": strArg(`which marker: "m1" (default) or "1"`)}), Annotations: readOnly(),
			Meta:   callableMeta("Looking at that moment…", "That moment"),
			onGame: playMoment},
		{Name: "play_state", Title: "A play's state at a moment",
			Description: "The game's state at a moment of a recorded play, replayed exactly: entities with their components, " +
				"session and save values, UI: the numbers behind what play_frame shows (positions, speeds, lives). parts narrows it like the driver's state: e.g. [\"session\"], [\"tag=Player\", \"TransformComponent\"].",
			InputSchema: object(map[string]any{"play": playRefArg, "at": atArg, "parts": listArg("which parts (default: all but the draw list)")}),
			Annotations: readOnly(), Meta: callableMeta("", ""),
			onGame: func(root string, a toolArgs) toolResult {
				p, _, f, err := openMoment(root, a.str("play"), a.str("at"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				state, err := stateAt(a.call(), root, p, f, a.list("parts"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(state)
			}},
		{Name: "play_inputs", Title: "What the person pressed",
			Description: "The keys and mouse buttons the person held in a recorded play, when and for how long (e.g. Space 0:12.3–0:12.7, 0.42 s). " +
				"Use it when how they played matters: a jump that felt floaty or short, a missed dodge. from/to are moments (default: the whole play); " +
				"play_show already lists what was pressed just before each marker.",
			InputSchema: object(map[string]any{"play": playRefArg, "from": fromArg, "to": atArg}), Annotations: readOnly(), Meta: callableMeta("", ""),
			onGame: func(root string, a toolArgs) toolResult {
				l, err := pressesBetween(root, a.str("play"), a.str("from"), a.str("to"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: l.text(), Structured: l}
			}},
		{Name: "play_verify", Title: "Does a play still replay the same?",
			Description: "After you change the game: replays the person's play (latest by default) with the current build and says whether it goes the same way, " +
				"and if not, by when and how its end differs (session values). Run it after build to check a change against how they actually played, " +
				"and tell the person both: after a change their play is expected to go differently; how it ends says what the change did. " +
				"To let them see it, play_show the play: at a marker, its \"See it now\" replays the moment with the change, beside what they saw.",
			InputSchema: object(map[string]any{"play": playRefArg}), Annotations: readOnly(), Meta: callableMeta("", ""),
			onGame: func(root string, a toolArgs) toolResult {
				p, b, err := openPlay(root, a.str("play"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				v, err := verify(a.call(), root, p, b)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: v.text(), Structured: v}
			}},
		{Name: "play_resume", Title: "Let the person play on from a moment",
			Description: "Opens the game for the person at a moment of a recorded play (fast-forwarded there, then theirs to play), " +
				"recorded as a new play. Use it to hand them the exact spot to try a change: \"play on from your marker 2\".",
			InputSchema: object(map[string]any{"play": playRefArg, "at": atArg}), Annotations: writes(),
			Meta:   callableMeta("Opening the game…", "The game is open"),
			onGame: playResume},
	}
}

func playShow(root string, a toolArgs) toolResult {
	p, b, err := openPlay(root, a.str("play"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	o, err := overviewOf(root, p, b)
	if err != nil {
		return textResult(err.Error(), true)
	}
	if at := a.str("at"); at != "" {
		f, err := p.FrameAt(at)
		if err != nil {
			return textResult(err.Error(), true)
		}
		o.At = &f
	}
	// The pictures go to the widget only (_meta): the model reads the summary.
	thumbs := []map[string]any{}
	for _, t := range thumbsToShow(o.Thumbs, 48) {
		if src := dataURI(t.Path, 200); src != "" {
			thumbs = append(thumbs, map[string]any{"frame": t.Frame, "time": t.Time, "src": src})
		}
	}
	markers := []map[string]any{}
	for _, m := range o.Markers {
		if data := jpegImage(momentPicture(p, m), 720); data != nil {
			markers = append(markers, map[string]any{"n": m.N, "src": jpegURI(data)})
		}
	}
	text := o.text() + "play_frame shows any moment; play_state gives the state there."
	if o.At != nil && o.Stale {
		text += fmt.Sprintf(" The timeline opens at frame %d and, where the host lets it, replays it with the current build beside what they saw.", *o.At)
	}
	o.Thumbs = nil // a path per second of play: the widget's images say it
	return toolResult{Text: text, Structured: o, Meta: map[string]any{"jm/thumbs": thumbs, "jm/markers": markers}}
}

// thumbsToShow keeps at most n thumbnails, evenly spread: a filmstrip, not a flipbook.
func thumbsToShow(all []plays.Thumb, n int) []plays.Thumb {
	if len(all) <= n {
		return all
	}
	out := make([]plays.Thumb, 0, n)
	for i := 0; i < n; i++ {
		out = append(out, all[i*(len(all)-1)/(n-1)])
	}
	return out
}

// markerImage is the picture taken at m, "" when it isn't a file of the
// play's own (only those are read).
func markerImage(p *plays.Play, m plays.Marker) string {
	if filepath.IsAbs(m.Image) {
		return ""
	}
	real, _, _ := realpath.Rel(p.Dir, m.Image)
	return real
}

func playFrame(root string, a toolArgs) toolResult {
	p, b, f, err := openMoment(root, a.str("play"), a.str("at"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	r, err := frameImage(a.call(), root, p, b, f, "")
	if err != nil {
		return textResult(err.Error(), true)
	}
	return imageResult(r.Path, fmt.Sprintf("Frame %d (%s) of play %s, %s.", f, clock(r.Time), p.ID, r.Source.text()), r)
}

// imageResult is an image for the model (a JPEG at most 960 wide) and the
// widget (_meta "jm/image"), with its text and structured description.
func imageResult(path, text string, structured any) toolResult {
	data := jpegOf(path, 960)
	if data == nil {
		return textResult("couldn't read the image "+path, true)
	}
	return toolResult{Text: text, Images: []mcpImage{{data, "image/jpeg"}}, Structured: structured,
		Meta: map[string]any{"jm/image": jpegURI(data)}}
}

func jpegURI(data []byte) string {
	return "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(data)
}

// playMoment is a marker's picture with its picks outlined, and the picks.
func playMoment(root string, a toolArgs) toolResult {
	p, err := plays.Find(root, a.str("play"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	m, err := markerOf(p, a.str("marker"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	text := momentSummary(p, m)
	// picked: each pick as the text says it, for pages that list them.
	picked := make([]string, len(m.Picks))
	for i, pick := range m.Picks {
		picked[i] = pick.Text()
	}
	structured := map[string]any{"play": p.ID, "marker": m, "picked": picked}
	data := jpegImage(momentPicture(p, m), 960)
	if data == nil {
		return toolResult{Text: text + "(its picture can't be read)", Structured: structured}
	}
	return toolResult{Text: text, Images: []mcpImage{{data, "image/jpeg"}}, Structured: structured, Meta: map[string]any{"jm/image": jpegURI(data)}}
}

// markerOf is a play's marker by "m2", "marker:2" or "2" ("": the first), as
// its summary has it (with what was pressed just before).
func markerOf(p *plays.Play, spec string) (plays.Marker, error) {
	s, err := p.Summarize()
	if err != nil {
		return plays.Marker{}, err
	}
	n := strings.TrimPrefix(strings.TrimPrefix(cmp.Or(spec, "m1"), "marker:"), "m")
	for _, m := range s.Markers {
		if strconv.Itoa(m.N) == n {
			return m, nil
		}
	}
	return plays.Marker{}, fmt.Errorf("play %s has no marker %s (it has %d)", p.ID, n, len(s.Markers))
}

// momentSummary is a marker for the model: when and where, the note, the
// picks, and where to look further.
func momentSummary(p *plays.Play, m plays.Marker) string {
	text := fmt.Sprintf("Marker %d of play %s at %s (frame %d), %s", m.N, p.ID, clock(m.Time), m.Frame, m.Scene)
	if m.Note != "" {
		text += fmt.Sprintf(": %q", m.Note)
	}
	text += "\n"
	if len(m.Pressed) > 0 {
		text += "    pressed just before: " + pressesText(m.Pressed) + "\n"
	}
	if len(m.Picks) > 0 {
		text += "The person picked these (outlined and numbered in the picture): what they mean, and a note after one is what they said about it. " +
			"An entity's origin says which file made it; a point or an area says what's there (entities, tile map cells).\n" + momentText(m)
	}
	return text + fmt.Sprintf("play_frame / play_state with play %q and at \"m%d\" show the frame replayed and its state.\n", p.ID, m.N)
}

func playResume(root string, a toolArgs) toolResult {
	return resumeIn(a.call(), root, root, a.str("play"), a.str("at"))
}

// resumeIn opens game in (root, or a variant or sandbox of it; built first if
// it needs it) for the person at a moment of root's play. It doesn't wait for them.
func resumeIn(c *toolCall, root, in, play, at string) toolResult {
	p, err := plays.Find(root, play)
	if err != nil {
		return textResult(err.Error(), true)
	}
	f, err := p.FrameAt(at)
	if err == nil {
		err = freshBuild(c, in)
	}
	if err != nil {
		return textResult(err.Error(), true)
	}
	self, err := os.Executable()
	if err != nil {
		return textResult(err.Error(), true)
	}
	cmd := exec.Command(self, "plays", "resume", p.ID, fmt.Sprint(f), "--in", in)
	cmd.Dir = root
	if err := cmd.Start(); err != nil {
		return textResult(err.Error(), true)
	}
	go func() { _ = cmd.Wait() }()
	changed := p.DriftFrom(plays.ReadBuild(filepath.Join(in, "build"))) == plays.GameChanged
	return toolResult{Text: resumedText(p.ID, f, changed), Structured: map[string]any{"play": p.ID, "frame": f, "opened": true, "gameChanged": changed}}
}

// resumedText says where the person was put. A changed game replays their
// inputs differently, so the spot may not be theirs: they should hear that.
func resumedText(play string, f uint64, changed bool) string {
	text := fmt.Sprintf("The game is open at frame %d of play %s; what they play from there is recorded as a new play (plays_list).", f, play)
	if changed {
		text += " The game changed since that play, so the replay there may have gone differently: tell them it may not be quite their spot (play_verify says by when it differs)."
	}
	return text
}

// jpegOf reads an image and gives it back as a JPEG at most maxWidth wide.
func jpegOf(path string, maxWidth int) []byte {
	return jpegImage(readImage(path), maxWidth)
}

// readImage is the image at path; nil if it can't be read.
func readImage(path string) image.Image {
	file, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer file.Close()
	// An image says its size first: one too big to be a frame isn't decoded.
	if config, _, err := image.DecodeConfig(file); err != nil || config.Width*config.Height > 8192*8192 {
		return nil
	}
	if _, err := file.Seek(0, 0); err != nil {
		return nil
	}
	img, _, err := image.Decode(file)
	if err != nil {
		return nil
	}
	return img
}

// jpegImage is img as a JPEG at most maxWidth wide; nil if it can't be one.
func jpegImage(img image.Image, maxWidth int) []byte {
	if img == nil {
		return nil
	}
	var out bytes.Buffer
	if err := jpeg.Encode(&out, shrink(img, maxWidth), &jpeg.Options{Quality: 80}); err != nil {
		return nil
	}
	return out.Bytes()
}

func dataURI(path string, maxWidth int) string {
	data := jpegOf(path, maxWidth)
	if data == nil {
		return ""
	}
	return jpegURI(data)
}

// shrink scales img down to at most maxWidth wide (a box filter), keeping its shape.
func shrink(img image.Image, maxWidth int) image.Image {
	b := img.Bounds()
	if b.Dx() <= maxWidth {
		return img
	}
	w := maxWidth
	h := b.Dy() * w / b.Dx()
	src := image.NewRGBA(b)
	draw.Draw(src, b, img, b.Min, draw.Src)
	dst := image.NewRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		y0, y1 := y*b.Dy()/h, max((y+1)*b.Dy()/h, y*b.Dy()/h+1)
		for x := 0; x < w; x++ {
			x0, x1 := x*b.Dx()/w, max((x+1)*b.Dx()/w, x*b.Dx()/w+1)
			var r, g, bl, n int
			for sy := y0; sy < y1; sy++ {
				for sx := x0; sx < x1; sx++ {
					i := sy*src.Stride + sx*4
					r += int(src.Pix[i])
					g += int(src.Pix[i+1])
					bl += int(src.Pix[i+2])
					n++
				}
			}
			j := y*dst.Stride + x*4
			dst.Pix[j], dst.Pix[j+1], dst.Pix[j+2], dst.Pix[j+3] = uint8(r/n), uint8(g/n), uint8(bl/n), 255
		}
	}
	return dst
}
