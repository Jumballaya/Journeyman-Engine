package main

import (
	"bytes"
	_ "embed"
	"encoding/base64"
	"encoding/json"
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
// agent looks at what they saw, at the moment they mean. In ChatGPT the
// play_show tool renders a timeline (widget/plays.html) the person can scrub,
// ask about and play on from.

//go:embed widget/plays.html
var playsWidget string

const playsWidgetURI = "ui://widget/journeyman-plays.html"

// widgetMeta marks a tool whose result the timeline renders, and tools the
// timeline itself may call.
func widgetMeta(renders bool, invoking, invoked string) map[string]any {
	meta := map[string]any{"openai/widgetAccessible": true}
	if renders {
		meta["openai/outputTemplate"] = playsWidgetURI
		meta["ui"] = map[string]any{"resourceUri": playsWidgetURI} // MCP Apps hosts
	}
	if invoking != "" {
		meta["openai/toolInvocation/invoking"] = invoking
		meta["openai/toolInvocation/invoked"] = invoked
	}
	return meta
}

func readOnly() map[string]any { return map[string]any{"readOnlyHint": true, "openWorldHint": false} }

func (s *mcpServer) playTools() []mcpTool {
	str := func(description string) map[string]any {
		return map[string]any{"type": "string", "description": description}
	}
	playArg := str(`which play: an id, a unique start of one, "latest" (default), or "-1", "-2" for earlier ones`)
	atArg := str(`a moment: a frame ("420"), a time ("12.5s", "1:05"), a marker ("marker:2"), "start" or "end" (default)`)
	return []mcpTool{
		{Name: "plays_list", Title: "List recorded plays",
			Description: "The plays the person recorded by playing the game (jm run records each one; F8 marks a moment), newest first: " +
				"id, length, markers, and whether it was made with an older build.",
			InputSchema: object(map[string]any{}), Annotations: readOnly(), Meta: widgetMeta(false, "", ""),
			run: func(map[string]any) toolResult { return jsonCommand("plays", "--json") }},
		{Name: "play_show", Title: "Show a play's timeline",
			Description: "What happened in a recorded play: the scenes over time, the game's values that changed (score, lives, ...), " +
				"the moments the person marked (with their notes), and thumbnails. In ChatGPT it opens a timeline the person can scrub. " +
				"Start here when the person talks about something that happened while they played.",
			InputSchema: object(map[string]any{"play": playArg}), Annotations: readOnly(),
			Meta: widgetMeta(true, "Opening the play…", "The play"),
			run:  s.playShow},
		{Name: "play_frame", Title: "See a moment of a play",
			Description: "An image of a moment in a recorded play, replayed exactly (or the nearest thumbnail where nothing can draw). " +
				"Use it to see what the person saw.",
			InputSchema: object(map[string]any{"play": playArg, "at": atArg}), Annotations: readOnly(),
			Meta: widgetMeta(false, "Replaying to that moment…", "That moment"),
			run:  s.playFrame},
		{Name: "play_state", Title: "A play's state at a moment",
			Description: "The game's state at a moment of a recorded play, replayed exactly: entities with their components, " +
				"session and save values, UI. parts narrows it like the driver's state: e.g. [\"session\"], [\"tag=Player\", \"TransformComponent\"].",
			InputSchema: object(map[string]any{"play": playArg, "at": atArg,
				"parts": map[string]any{"type": "array", "items": map[string]any{"type": "string"}}}),
			Annotations: readOnly(), Meta: widgetMeta(false, "", ""),
			run: func(a map[string]any) toolResult {
				return jsonCommand(append([]string{"plays", "state", argString(a, "play"), argString(a, "at")}, stringList(a["parts"])...)...)
			}},
		{Name: "play_verify", Title: "Does a play still replay the same?",
			Description: "Replays a recorded play to its end with the current build and says whether it goes the same way, " +
				"and from which frame it doesn't: after changing the game, what would the person's play do now?",
			InputSchema: object(map[string]any{"play": playArg}), Annotations: readOnly(), Meta: widgetMeta(false, "", ""),
			run: func(a map[string]any) toolResult {
				return jsonCommand("plays", "verify", argString(a, "play"), "--json")
			}},
		{Name: "play_resume", Title: "Let the person play on from a moment",
			Description: "Opens the game for the person at a moment of a recorded play (fast-forwarded there, then theirs to play), " +
				"recorded as a new play. Use it to hand them the exact spot to try a change: \"play on from your marker 2\".",
			InputSchema: object(map[string]any{"play": playArg, "at": atArg}),
			Annotations: map[string]any{"readOnlyHint": false, "openWorldHint": false, "destructiveHint": false},
			Meta:        widgetMeta(false, "Opening the game…", "The game is open"),
			run:         playResume},
	}
}

func argString(a map[string]any, key string) string {
	v, _ := a[key].(string)
	return v
}

// jsonCommand runs a jm command whose output is JSON: the model reads it, a
// host gets it as structured content too.
func jsonCommand(args ...string) toolResult {
	clean := args[:0:0]
	for _, a := range args {
		if a != "" {
			clean = append(clean, a)
		}
	}
	out, failed := runJM(clean...)
	r := textResult(out, failed)
	var structured any
	if !failed && json.Unmarshal([]byte(out), &structured) == nil {
		if _, isObject := structured.(map[string]any); isObject {
			r.Structured = structured
		} else {
			r.Structured = map[string]any{"items": structured} // structuredContent is an object
		}
	}
	return r
}

func (s *mcpServer) playShow(a map[string]any) toolResult {
	root, p, err := findPlay(argString(a, "play"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	summary, err := p.Summarize()
	if err != nil {
		return textResult(err.Error(), true)
	}
	stale := recordedBuild(p) != "" && recordedBuild(p) != buildFingerprint(filepath.Join(root, "build"))

	// The model reads the summary; the widget also gets the images.
	thumbs := []map[string]any{}
	for _, t := range thumbsToShow(summary.Thumbs, 48) {
		if src := dataURI(t.Path, 200); src != "" {
			thumbs = append(thumbs, map[string]any{"frame": t.Frame, "time": t.Time, "src": src})
		}
	}
	markers := []map[string]any{}
	for _, m := range summary.Markers {
		if src := dataURI(filepath.Join(p.Dir, m.Image), 720); src != "" {
			markers = append(markers, map[string]any{"n": m.N, "src": src})
		}
	}
	modelThumbs := make([]map[string]any, 0, len(summary.Thumbs))
	for _, t := range summary.Thumbs {
		modelThumbs = append(modelThumbs, map[string]any{"frame": t.Frame, "time": t.Time})
	}
	structured := map[string]any{
		"play": summary.ID, "game": summary.Game, "started": summary.Started, "seconds": summary.Seconds,
		"frames": summary.Frames, "ended": summary.Ended, "stale": stale, "markers": summary.Markers,
		"scenes": summary.Scenes, "values": summary.Values, "sampleTime": summary.SampleT, "thumbs": modelThumbs,
	}
	var text strings.Builder
	fmt.Fprintf(&text, "Play %s: %s of %s (%d frames, %s).", summary.ID, clock(summary.Seconds), summary.Game, summary.Frames, summary.Ended)
	if stale {
		text.WriteString(" Made with an older build: replays may differ (play_verify).")
	}
	for _, span := range summary.Scenes {
		fmt.Fprintf(&text, "\n%s–%s %s", clock(span.From), clock(span.To), span.Scene)
	}
	for _, v := range summary.Values {
		fmt.Fprintf(&text, "\n%s: %g → %g (min %g, max %g)", v.Key, v.First, v.Last, v.Min, v.Max)
	}
	for _, m := range summary.Markers {
		fmt.Fprintf(&text, "\nmarker %d at %s (frame %d, %s)", m.N, clock(m.Time), m.Frame, m.Scene)
		if m.Note != "" {
			fmt.Fprintf(&text, ": %q", m.Note)
		}
	}
	text.WriteString("\nplay_frame shows any moment; play_state gives the state there.")
	return toolResult{Text: text.String(), Structured: structured,
		Meta: map[string]any{"jm/thumbs": thumbs, "jm/markers": markers}}
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

func (s *mcpServer) playFrame(a map[string]any) toolResult {
	root, p, f, err := momentOf(argString(a, "play"), argString(a, "at"))
	if err != nil {
		return textResult(err.Error(), true)
	}
	path, source, err := playImage(root, p, f, "")
	if err != nil {
		return textResult(err.Error(), true)
	}
	data := jpegOf(path, 960)
	if data == nil {
		return textResult("couldn't read "+path, true)
	}
	times, _ := p.Times()
	t := 0.0
	if int(f) < len(times) {
		t = times[f]
	}
	return toolResult{
		Text:       fmt.Sprintf("Frame %d (%s) of play %s, %s.", f, clock(t), p.ID, source),
		Images:     []mcpImage{{data, "image/jpeg"}},
		Structured: map[string]any{"play": p.ID, "frame": f, "time": t, "source": source, "path": path},
		Meta:       map[string]any{"jm/image": "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(data)},
	}
}

func playResume(a map[string]any) toolResult {
	_, p, f, err := momentOf(argString(a, "play"), argString(a, "at"))
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

// playsWidgetResource is the timeline as a resource a host renders.
func playsWidgetResource() map[string]any {
	return map[string]any{
		"uri": playsWidgetURI, "name": "journeyman-plays", "title": "Play timeline",
		"description": "A recorded play: scrub its moments, see what the player saw, ask about one or play on from it.",
		"mimeType":    "text/html+skybridge",
		"_meta": map[string]any{
			"openai/widgetDescription":   "A timeline of the person's recorded play: thumbnails, scenes, values over time and their marked moments.",
			"openai/widgetPrefersBorder": true,
			"openai/widgetCSP":           map[string]any{"connect_domains": []string{}, "resource_domains": []string{}},
		},
	}
}
