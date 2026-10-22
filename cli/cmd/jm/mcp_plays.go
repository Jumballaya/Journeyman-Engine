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
)

// The recorded plays over MCP: the person plays (jm run records), and the
// agent looks at what they saw, at the moment they mean. play_show renders a
// timeline (widget/plays.html) the person can scrub, ask about and play on from.

//go:embed widget/plays.html
var playsPage string

var timeline = widget{name: "journeyman-plays", title: "Play timeline", html: playsPage, modes: []string{"inline", "fullscreen"},
	description: "A recorded play: scrub its moments, see what the player saw, ask about one or play on from it."}

func (s *mcpServer) playTools() []mcpTool {
	playArg := strArg(`which play: an id, a unique start of one, "latest" (default), or "latest-1", "latest-2" for earlier ones`)
	moment := `a moment: a frame ("420"), a time ("12.5s", "1:05"), a marker ("m2" or "marker:2"), `
	atArg, fromArg := strArg(moment+`"start" or "end" (default)`), strArg(moment+`"start" (default) or "end"`)
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
				"Then play_frame / play_state at a marker.",
			InputSchema: object(map[string]any{"play": playArg}), Annotations: readOnly(),
			Meta: timeline.toolMeta("Opening the play…", "The play", entrypoint("thread")),
			run:  playShow},
		{Name: "play_frame", Title: "See a moment of a play",
			Description: "An image of a moment in a recorded play, replayed exactly (or the nearest thumbnail where nothing can draw: source says which). " +
				"Use it to see what the person saw, e.g. at: \"m1\" for their first marker.",
			InputSchema: object(map[string]any{"play": playArg, "at": atArg}), Annotations: readOnly(),
			Meta: callableMeta("Replaying to that moment…", "That moment"),
			run:  playFrame},
		{Name: "play_moment", Title: "What the person pointed at",
			Description: "A marker the person saved with F8 after picking entities, points or areas (a moment): the frame as they saw it with each pick outlined and numbered, " +
				"and the picks: each entity's id, tags, components then and origin, where it came from: a scene's entry (scene, entry, name, prefab) " +
				"or a spawn (the prefab, the frame, the entity and script that spawned it). A point or an area (empty space: \"a coin should be here\", \"this section is too dark\") " +
				"has its world coordinates (y up), the entities there with their origins, and the tile map cells it covers (map, layer, tile coordinates from the bottom row, gids). " +
				"What they picked is what they meant; origin, or the map, is the file to change. " +
				"play_show lists every marker's picks.",
			InputSchema: object(map[string]any{"play": playArg, "marker": strArg(`which marker: "m1" (default) or "1"`)}), Annotations: readOnly(),
			Meta:   callableMeta("Looking at that moment…", "That moment"),
			onGame: playMoment},
		{Name: "play_state", Title: "A play's state at a moment",
			Description: "The game's state at a moment of a recorded play, replayed exactly: entities with their components, " +
				"session and save values, UI: the numbers behind what play_frame shows (positions, speeds, lives). parts narrows it like the driver's state: e.g. [\"session\"], [\"tag=Player\", \"TransformComponent\"].",
			InputSchema: object(map[string]any{"play": playArg, "at": atArg, "parts": listArg("which parts (default: all but the draw list)")}),
			Annotations: readOnly(), Meta: callableMeta("", ""),
			run: func(a toolArgs) toolResult {
				root, p, _, f, err := openMoment(a.str("play"), a.str("at"))
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
			InputSchema: object(map[string]any{"play": playArg, "from": fromArg, "to": atArg}), Annotations: readOnly(), Meta: callableMeta("", ""),
			run: func(a toolArgs) toolResult {
				l, err := pressesBetween(a.str("play"), a.str("from"), a.str("to"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: l.text(), Structured: l}
			}},
		{Name: "play_verify", Title: "Does a play still replay the same?",
			Description: "After you change the game: replays the person's play (latest by default) with the current build and says whether it goes the same way, " +
				"and if not, by when and how its end differs (session values). Run it after build to check a change against how they actually played, " +
				"and tell the person both: after a change their play is expected to go differently; how it ends says what the change did.",
			InputSchema: object(map[string]any{"play": playArg}), Annotations: readOnly(), Meta: callableMeta("", ""),
			run: func(a toolArgs) toolResult {
				root, p, b, err := openPlay(a.str("play"))
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
			InputSchema: object(map[string]any{"play": playArg, "at": atArg}), Annotations: writes(),
			Meta: callableMeta("Opening the game…", "The game is open"),
			run:  playResume},
	}
}

func playShow(a toolArgs) toolResult {
	_, p, b, err := openPlay(a.str("play"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	o, err := overviewOf(p, b)
	if err != nil {
		return textResult(err.Error(), true)
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
	path := filepath.Join(p.Dir, m.Image)
	dir, err := realPath(p.Dir)
	if err != nil || !filepath.IsLocal(m.Image) {
		return ""
	}
	real, err := realPath(path)
	if rel, relErr := filepath.Rel(dir, real); err != nil || relErr != nil || !filepath.IsLocal(rel) {
		return ""
	}
	return path
}

func playFrame(a toolArgs) toolResult {
	root, p, b, f, err := openMoment(a.str("play"), a.str("at"))
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

func playResume(a toolArgs) toolResult {
	_, p, _, f, err := openMoment(a.str("play"), a.str("at"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	self, err := os.Executable()
	if err != nil {
		return textResult(err.Error(), true)
	}
	// The person plays; the call doesn't wait for them to finish.
	cmd := exec.Command(self, "plays", "resume", p.ID, fmt.Sprint(f))
	if err := cmd.Start(); err != nil {
		return textResult(err.Error(), true)
	}
	go func() { _ = cmd.Wait() }()
	return toolResult{Text: fmt.Sprintf("The game is open at frame %d of play %s; what they play from there is recorded as a new play (plays_list).", f, p.ID),
		Structured: map[string]any{"play": p.ID, "frame": f, "opened": true}}
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
