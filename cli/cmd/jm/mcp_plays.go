package main

import (
	"bytes"
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
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// The recorded plays over MCP: the person plays (jm run records), and the
// agent looks at what they saw, at the moment they mean. play_show renders a
// timeline (widget/plays.html) the person can scrub, ask about and play on from.

//go:embed widget/plays.html
var playsWidget string

// timelineMeta marks a tool whose result the timeline renders: ChatGPT's
// widget, and the same page as an MCP App for hosts like Claude.
func timelineMeta(invoking, invoked string) map[string]any {
	meta := callableMeta(invoking, invoked)
	meta["openai/outputTemplate"] = timelineWidget.uri
	meta["ui"] = map[string]any{"resourceUri": timelineApp.uri}
	return meta
}

// callableMeta marks a tool the timeline may call itself, with what the host
// says while it runs (none: "").
func callableMeta(invoking, invoked string) map[string]any {
	meta := map[string]any{"openai/widgetAccessible": true}
	if invoking != "" {
		meta["openai/toolInvocation/invoking"] = invoking
		meta["openai/toolInvocation/invoked"] = invoked
	}
	return meta
}

func (s *mcpServer) playTools() []mcpTool {
	playArg := strArg(`which play: an id, a unique start of one, "latest" (default), or "latest-1", "latest-2" for earlier ones`)
	atArg := strArg(`a moment: a frame ("420"), a time ("12.5s", "1:05"), a marker ("m2" or "marker:2"), "start" or "end" (default)`)
	return []mcpTool{
		{Name: "plays_list", Title: "List recorded plays",
			Description: "Every play the person recorded, newest first: id, length, when each marker is, and whether an older build made it. " +
				"For the latest play, call play_show instead (it shows the person the timeline).",
			InputSchema: object(map[string]any{}), Annotations: readOnly(), Meta: callableMeta("", ""),
			run: func(toolArgs) toolResult {
				root, err := projectRoot()
				if err != nil {
					return textResult(err.Error(), true)
				}
				listing, err := listPlays(root)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(listing)
			}},
		{Name: "play_show", Title: "Show a play's timeline",
			Description: "Call first whenever the person talks about playing the game: how it felt, a bug, a death, a marker, \"just now\", " +
				"or how the game is going. Shows the person their play as a timeline they can scrub (latest play by default), " +
				"and gives you what happened: scenes over time, values that changed (lives, score, ...), and the moments they marked with F8 (what they meant is in what they tell you). " +
				"Then play_frame / play_state at a marker.",
			InputSchema: object(map[string]any{"play": playArg}), Annotations: readOnly(),
			Meta: timelineMeta("Opening the play…", "The play"),
			run:  playShow},
		{Name: "play_frame", Title: "See a moment of a play",
			Description: "An image of a moment in a recorded play, replayed exactly (or the nearest thumbnail where nothing can draw: source says which). " +
				"Use it to see what the person saw, e.g. at: \"m1\" for their first marker.",
			InputSchema: object(map[string]any{"play": playArg, "at": atArg}), Annotations: readOnly(),
			Meta: callableMeta("Replaying to that moment…", "That moment"),
			run:  playFrame},
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
				state, err := stateAt(root, p, f, a.list("parts"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(state)
			}},
		{Name: "play_inputs", Title: "What the person pressed",
			Description: "The keys and mouse buttons the person held in a recorded play, when and for how long (e.g. Space 0:12.3–0:12.7, 0.42 s). " +
				"Use it when how they played matters: a jump that felt floaty or short, a missed dodge. from/to are moments (default: the whole play); " +
				"play_show already lists what was pressed just before each marker.",
			InputSchema: object(map[string]any{"play": playArg, "from": atArg, "to": atArg}), Annotations: readOnly(), Meta: callableMeta("", ""),
			run: func(a toolArgs) toolResult {
				l, err := pressesBetween(a.str("play"), a.str("from"), a.str("to"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				return toolResult{Text: l.text(), Structured: l}
			}},
		{Name: "play_verify", Title: "Does a play still replay the same?",
			Description: "After you change the game: replays the person's play (latest by default) with the current build and says whether it goes the same way, " +
				"and from which frame it doesn't. Run it after build to check a change against how they actually played.",
			InputSchema: object(map[string]any{"play": playArg}), Annotations: readOnly(), Meta: callableMeta("", ""),
			run: func(a toolArgs) toolResult {
				root, p, b, err := openPlay(a.str("play"))
				if err != nil {
					return textResult(err.Error(), true)
				}
				v, err := verify(root, p, b)
				if err != nil {
					return textResult(err.Error(), true)
				}
				return jsonResult(v)
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
		if !filepath.IsLocal(m.Image) {
			continue // a marker's image is in its play's folder
		}
		if src := dataURI(filepath.Join(p.Dir, m.Image), 720); src != "" {
			markers = append(markers, map[string]any{"n": m.N, "src": src})
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

func playFrame(a toolArgs) toolResult {
	root, p, b, f, err := openMoment(a.str("play"), a.str("at"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	r, err := frameImage(root, p, b, f, "")
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
		Meta: map[string]any{"jm/image": "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(data)}}
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
	img = shrink(img, maxWidth)
	var out bytes.Buffer
	if err := jpeg.Encode(&out, img, &jpeg.Options{Quality: 80}); err != nil {
		return nil
	}
	return out.Bytes()
}

func dataURI(path string, maxWidth int) string {
	data := jpegOf(path, maxWidth)
	if data == nil {
		return ""
	}
	return "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(data)
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

// timelineResource is the timeline page as a resource of one host kind.
type timelineResource struct {
	uri, name, mimeType string
	meta                map[string]any
}

var (
	timelineWidget = timelineResource{"ui://widget/journeyman-plays.html", "journeyman-plays", "text/html+skybridge", map[string]any{
		"openai/widgetDescription":   "A timeline of the person's recorded play: thumbnails, scenes, values over time and their marked moments.",
		"openai/widgetPrefersBorder": true,
		"openai/widgetCSP":           map[string]any{"connect_domains": []string{}, "resource_domains": []string{}},
	}}
	timelineApp = timelineResource{"ui://widget/journeyman-plays-app.html", "journeyman-plays-app", "text/html;profile=mcp-app", map[string]any{
		"ui": map[string]any{"prefersBorder": true, "csp": map[string]any{"connectDomains": []string{}, "resourceDomains": []string{}}},
	}}
)

func timelineResources() []map[string]any {
	var out []map[string]any
	for _, r := range []timelineResource{timelineWidget, timelineApp} {
		out = append(out, map[string]any{"uri": r.uri, "name": r.name, "title": "Play timeline", "mimeType": r.mimeType, "_meta": r.meta,
			"description": "A recorded play: scrub its moments, see what the player saw, ask about one or play on from it."})
	}
	return out
}

// readTimeline is the timeline page for uri, stamped with this jm's version
// (it warns when a host shows an older copy than the server); false: not one.
func readTimeline(uri string) (map[string]any, bool) {
	for _, r := range []timelineResource{timelineWidget, timelineApp} {
		if uri == r.uri {
			return map[string]any{"uri": uri, "mimeType": r.mimeType, "_meta": r.meta,
				"text": strings.ReplaceAll(playsWidget, "__JM_VERSION__", version)}, true
		}
	}
	return nil, false
}
