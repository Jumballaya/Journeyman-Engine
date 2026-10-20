package atlas

import (
	"bytes"
	"encoding/json"
	"fmt"
	"image"
	"image/draw"
	"slices"
	"strconv"
)

// Clip is a flipbook animation in a built atlas: its frames' regions, each
// shown for its own seconds, in play order (ping-pong already unrolled).
type Clip struct {
	Frames []ClipFrame `json:"frames"`
	Loop   bool        `json:"loop"`
}

type ClipFrame struct {
	Region   string  `json:"region"`
	Duration float64 `json:"duration"` // seconds
}

// Sheet is an Aseprite sprite sheet export's JSON data (File > Export Sprite
// Sheet, "Array" or "Hash" frames, with tags): where each frame is and its tags.
type Sheet struct {
	Image  string // the sheet's PNG, relative to the JSON
	frames []sheetFrame
	tags   []sheetTag
}

type rect struct{ X, Y, W, H int }

type sheetFrame struct {
	Frame            rect               `json:"frame"`
	Rotated          bool               `json:"rotated"`
	SpriteSourceSize rect               `json:"spriteSourceSize"` // where a trimmed frame sits in the full one
	SourceSize       struct{ W, H int } `json:"sourceSize"`
	Duration         int                `json:"duration"` // milliseconds
}

type sheetTag struct {
	Name      string `json:"name"`
	From      int    `json:"from"`
	To        int    `json:"to"`
	Direction string `json:"direction"` // forward, reverse, pingpong, pingpong_reverse
	Repeat    string `json:"repeat"`    // times to play ("" or "0": forever)
}

// ParseSheet reads an Aseprite export's JSON.
func ParseSheet(data []byte) (Sheet, error) {
	var raw struct {
		Frames json.RawMessage `json:"frames"`
		Meta   struct {
			App       string     `json:"app"`
			Image     string     `json:"image"`
			FrameTags []sheetTag `json:"frameTags"`
		} `json:"meta"`
	}
	if err := json.Unmarshal(data, &raw); err != nil {
		return Sheet{}, fmt.Errorf("aseprite: %w", err)
	}
	if raw.Meta.Image == "" {
		return Sheet{}, fmt.Errorf("aseprite: no meta.image: export the sheet with JSON data from Aseprite")
	}
	frames, err := orderedFrames(raw.Frames)
	if err != nil {
		return Sheet{}, err
	}
	for i, f := range frames {
		if f.Rotated {
			return Sheet{}, fmt.Errorf("aseprite: frame %d is rotated: export without rotation", i)
		}
	}
	for _, t := range raw.Meta.FrameTags {
		if t.From < 0 || t.To >= len(frames) || t.From > t.To {
			return Sheet{}, fmt.Errorf("aseprite: tag %q spans frames %d..%d of %d", t.Name, t.From, t.To, len(frames))
		}
	}
	return Sheet{Image: raw.Meta.Image, frames: frames, tags: raw.Meta.FrameTags}, nil
}

// orderedFrames reads "frames" as an array, or as a hash in the file's order.
func orderedFrames(data json.RawMessage) ([]sheetFrame, error) {
	var frames []sheetFrame
	if bytes.HasPrefix(bytes.TrimSpace(data), []byte("[")) {
		err := json.Unmarshal(data, &frames)
		return frames, wrap(err)
	}
	dec := json.NewDecoder(bytes.NewReader(data))
	if _, err := dec.Token(); err != nil { // {
		return nil, wrap(err)
	}
	for dec.More() {
		if _, err := dec.Token(); err != nil { // the frame's name
			return nil, wrap(err)
		}
		var f sheetFrame
		if err := dec.Decode(&f); err != nil {
			return nil, wrap(err)
		}
		frames = append(frames, f)
	}
	return frames, nil
}

func wrap(err error) error {
	if err == nil {
		return nil
	}
	return fmt.Errorf("aseprite: frames: %w", err)
}

// Cut cuts the sheet's frames out of img (and normal, when not nil) as
// sources named name_0, name_1... at their untrimmed size, and makes a clip
// per tag (one "default" clip of every frame when it has none).
func (s Sheet) Cut(name string, img, normal image.Image) ([]SourceImage, map[string]Clip, error) {
	sources := make([]SourceImage, len(s.frames))
	for i, f := range s.frames {
		r := image.Rect(f.Frame.X, f.Frame.Y, f.Frame.X+f.Frame.W, f.Frame.Y+f.Frame.H)
		if !r.In(img.Bounds()) {
			return nil, nil, fmt.Errorf("aseprite: frame %d (%v) is outside the %v sheet", i, r, img.Bounds().Size())
		}
		sources[i] = SourceImage{Name: frameName(name, i), Img: untrimmed(img, r, f)}
		if normal != nil {
			sources[i].Normal = untrimmed(normal, r, f)
		}
	}
	tags := s.tags
	if len(tags) == 0 {
		tags = []sheetTag{{Name: "default", To: len(s.frames) - 1}}
	}
	clips := map[string]Clip{}
	for _, t := range tags {
		clip := Clip{Loop: atoi(t.Repeat) <= 0}
		for _, i := range playOrder(t) {
			clip.Frames = append(clip.Frames, ClipFrame{frameName(name, i), float64(max(1, s.frames[i].Duration)) / 1000})
		}
		clips[t.Name] = clip
	}
	return sources, clips, nil
}

// Frame cuts frame i out of img as Cut does, alone; an error for one that
// isn't there, isn't inside img, or would be over side pixels on a side.
func (s Sheet) Frame(i int, img image.Image, side int) (image.Image, error) {
	if i < 0 || i >= len(s.frames) {
		return nil, fmt.Errorf("aseprite: no frame %d", i)
	}
	f := s.frames[i]
	r := image.Rect(f.Frame.X, f.Frame.Y, f.Frame.X+f.Frame.W, f.Frame.Y+f.Frame.H)
	if !r.In(img.Bounds()) {
		return nil, fmt.Errorf("aseprite: frame %d (%v) is outside the %v sheet", i, r, img.Bounds().Size())
	}
	content := image.Pt(f.SpriteSourceSize.W, f.SpriteSourceSize.H)
	if content.X <= 0 || content.Y <= 0 {
		content = r.Size()
	}
	if max(f.SourceSize.W, content.X) > side || max(f.SourceSize.H, content.Y) > side {
		return nil, fmt.Errorf("aseprite: frame %d is over %d pixels on a side", i, side)
	}
	return untrimmed(img, r, f), nil
}

// untrimmed is the frame at its full size, its trimmed pixels where Aseprite
// cut them from (r includes any inner padding around them).
func untrimmed(sheet image.Image, r image.Rectangle, f sheetFrame) image.Image {
	content := image.Pt(f.SpriteSourceSize.W, f.SpriteSourceSize.H)
	if content.X <= 0 || content.Y <= 0 {
		content = r.Size()
	}
	from := r.Min.Add(r.Size().Sub(content).Div(2))
	out := image.NewNRGBA(image.Rect(0, 0, max(f.SourceSize.W, content.X), max(f.SourceSize.H, content.Y)))
	at := image.Pt(f.SpriteSourceSize.X, f.SpriteSourceSize.Y)
	draw.Draw(out, image.Rectangle{at, at.Add(content)}, sheet, from, draw.Src)
	return out
}

// playOrder is a tag's frames as played, as Aseprite does: reversed, there
// and back (the turn's frame not shown twice), and its repeat count of passes
// (each way one pass); a looping ping-pong is one there and back.
func playOrder(t sheetTag) []int {
	var pass []int
	for i := t.From; i <= t.To; i++ {
		pass = append(pass, i)
	}
	if t.Direction == "reverse" || t.Direction == "pingpong_reverse" {
		slices.Reverse(pass)
	}
	passes := max(1, atoi(t.Repeat))
	pingpong := t.Direction == "pingpong" || t.Direction == "pingpong_reverse"
	if pingpong && atoi(t.Repeat) <= 0 {
		passes = 2
	}
	order := slices.Clone(pass)
	for k := 1; k < passes; k++ {
		if !pingpong {
			order = append(order, pass...)
			continue
		}
		slices.Reverse(pass)
		order = append(order, pass[1:]...)
	}
	if pingpong && atoi(t.Repeat) <= 0 && len(order) > 1 {
		order = order[:len(order)-1] // looping: the start isn't shown twice either
	}
	return order
}

func frameName(name string, i int) string { return name + "_" + strconv.Itoa(i) }

func atoi(s string) int {
	n, _ := strconv.Atoi(s)
	return n
}

// SheetFrame is a frame as checks see it: its untrimmed size and milliseconds.
type SheetFrame struct{ W, H, Milliseconds int }

// SheetTag is a tag's name and its frames, first to last.
type SheetTag struct {
	Name     string
	From, To int
}

func (s Sheet) Frames() []SheetFrame {
	out := make([]SheetFrame, len(s.frames))
	for i, f := range s.frames {
		out[i] = SheetFrame{f.SourceSize.W, f.SourceSize.H, f.Duration}
	}
	return out
}

// Outside is the first frame that isn't inside a w by h picture, -1 if none.
func (s Sheet) Outside(w, h int) int {
	return slices.IndexFunc(s.frames, func(f sheetFrame) bool {
		return !image.Rect(f.Frame.X, f.Frame.Y, f.Frame.X+f.Frame.W, f.Frame.Y+f.Frame.H).In(image.Rect(0, 0, w, h))
	})
}

func (s Sheet) Tags() []SheetTag {
	out := make([]SheetTag, len(s.tags))
	for i, t := range s.tags {
		out[i] = SheetTag{t.Name, t.From, t.To}
	}
	return out
}
