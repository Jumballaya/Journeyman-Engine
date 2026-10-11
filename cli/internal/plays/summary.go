package plays

import (
	"encoding/json"
	"math"
	"sort"
)

// Span is a stretch of the play spent in one scene.
type Span struct {
	Scene     string  `json:"scene"`
	FromFrame uint64  `json:"fromFrame"`
	ToFrame   uint64  `json:"toFrame"` // the last sample's frame in it
	From      float64 `json:"from"`    // seconds
	To        float64 `json:"to"`
}

// Series is a numeric session value over the play, one point per sample.
type Series struct {
	Key    string    `json:"key"`
	Points []float64 `json:"points"`
	Since  int       `json:"since,omitempty"` // the first point the game had set it (earlier points repeat it)
	First  float64   `json:"first"`
	Last   float64   `json:"last"`
	Min    float64   `json:"min"`
	Max    float64   `json:"max"`
}

// Summary is a play at a glance: what tools show a person, and an agent reads
// before replaying anything.
type Summary struct {
	ID       string    `json:"id"`
	Game     string    `json:"game"`
	Started  string    `json:"started"`
	Seconds  float64   `json:"seconds"`
	Frames   uint64    `json:"frames"`
	Ended    string    `json:"ended"`
	Gamepad  bool      `json:"gamepad,omitempty"`
	Markers  []Marker  `json:"markers"`
	Scenes   []Span    `json:"scenes"`
	Values   []Series  `json:"values"`     // numeric session values that changed or were set partway through
	SampleAt []uint64  `json:"sampleAt"`   // the frame of each series point
	SampleT  []float64 `json:"sampleTime"` // its time
	Thumbs   []Thumb   `json:"thumbs,omitempty"`
}

// Summarize reads the play's timeline into a Summary.
func (p *Play) Summarize() (Summary, error) {
	s := Summary{ID: p.ID, Game: p.Meta.Game, Started: p.Meta.Started, Seconds: p.Meta.Seconds,
		Frames: p.Meta.Frames, Ended: p.Meta.Ended, Gamepad: p.Meta.Gamepad, Markers: p.Meta.Markers,
		Scenes: []Span{}, Values: []Series{}}
	s.Markers = append([]Marker{}, s.Markers...) // its own: their times are set below
	for i := range s.Markers {
		m := &s.Markers[i]
		at := m.Time // the engine's: kept for one past the frames a crash kept
		// When its frame started, as every other time here is (the engine notes the end).
		if t := p.TimeOf(m.Frame); t > 0 || m.Frame == 0 {
			at, m.Time = t, ms(t)
		}
		m.Pressed, _ = p.Presses(p.FrameRunning(at-LeadIn), m.Frame) // none recorded: none shown
	}
	s.Thumbs = p.Thumbs() // an unreadable frames.bin: thumbnails at time 0, the rest stands
	for i := range s.Thumbs {
		s.Thumbs[i].Time = ms(s.Thumbs[i].Time)
	}
	if s.Thumbs == nil {
		s.Thumbs = []Thumb{}
	}
	for _, m := range s.Markers {
		s.Frames = max(s.Frames, m.Frame+1)
	}
	samples, err := p.Samples()
	if err != nil {
		return s, nil // no timeline yet: what session.json says is the summary
	}
	numbers := map[string][]float64{}
	since := map[string]int{}
	var keys []string
	for i, sample := range samples {
		// Play time, as moments and markers are: the sample's own clock is the
		// game's, which stops while it's paused.
		sample.Time = p.TimeOf(sample.Frame)
		s.SampleAt = append(s.SampleAt, sample.Frame)
		s.SampleT = append(s.SampleT, ms(sample.Time))
		// What the player sees: mid-transition, still the scene being left.
		scene := sample.Scene
		if sample.From != "" {
			scene = sample.From
		}
		if n := len(s.Scenes); n == 0 || s.Scenes[n-1].Scene != scene {
			s.Scenes = append(s.Scenes, Span{Scene: scene, FromFrame: sample.Frame, From: ms(sample.Time)})
		}
		last := &s.Scenes[len(s.Scenes)-1]
		last.ToFrame, last.To = sample.Frame, ms(sample.Time)
		for key, raw := range sample.Session {
			var v float64
			if json.Unmarshal(raw, &v) != nil {
				continue
			}
			if _, seen := numbers[key]; !seen {
				keys = append(keys, key)
				// Not set before (a title screen has no score yet): those points
				// repeat its first value, and the series starts here.
				numbers[key] = make([]float64, i, len(samples))
				for j := range numbers[key] {
					numbers[key][j] = v
				}
				since[key] = i
			}
			for len(numbers[key]) < i {
				numbers[key] = append(numbers[key], numbers[key][len(numbers[key])-1])
			}
			numbers[key] = append(numbers[key], v)
		}
	}
	sort.Strings(keys)
	for _, key := range keys {
		points := numbers[key]
		for len(points) < len(samples) {
			points = append(points, points[len(points)-1])
		}
		from := since[key]
		series := Series{Key: key, Points: points, Since: from, First: points[from], Last: points[len(points)-1], Min: points[from], Max: points[from]}
		for _, v := range points[from:] {
			series.Min = min(series.Min, v)
			series.Max = max(series.Max, v)
		}
		// Set partway through is a change too: a game often sets "deaths" at
		// the first one.
		if series.Min != series.Max || from > 0 {
			s.Values = append(s.Values, series)
		}
	}
	// session.json's count is written every second: a play that crashed has
	// samples past it (and markers, above). What the play shows covers them.
	if n := len(s.SampleAt); n > 0 {
		s.Frames = max(s.Frames, s.SampleAt[n-1]+1)
	}
	return s, nil
}

// ms rounds a time to the millisecond: what a person or a model reads of it
// (the float32 sums otherwise print as 0.5166666936129332).
func ms(seconds float64) float64 { return math.Round(seconds*1000) / 1000 }
