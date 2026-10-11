// Package plays reads recorded plays: the play sessions the engine writes
// when `jm run` records (JM_RECORD_DIR; the format is in the engine's
// PlaySession.hpp). A play is a folder in .jm/plays/<id>.
package plays

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
)

// Folder is where a project keeps its plays, relative to its root.
const Folder = ".jm/plays"

// Marker is a moment the player marked (F8) or a tool did (the driver's marker).
type Marker struct {
	N     int     `json:"n"`
	Frame uint64  `json:"frame"`
	Time  float64 `json:"time"`
	Scene string  `json:"scene"`
	Note  string  `json:"note,omitempty"`
	Image string  `json:"image"` // relative to the play's folder
}

// Meta is session.json.
type Meta struct {
	Format        int             `json:"format"`
	Game          string          `json:"game"`
	GameVersion   string          `json:"gameVersion"`
	Started       string          `json:"started"`
	Frames        uint64          `json:"frames"`
	Seconds       float64         `json:"seconds"`
	Seed          uint64          `json:"seed"`
	EntryScene    string          `json:"entryScene"`
	SessionValues json.RawMessage `json:"sessionValues,omitempty"`
	Markers       []Marker        `json:"markers"`
	Ended         string          `json:"ended"` // quit, running (still going, or crashed)
	Gamepad       bool            `json:"gamepad"`
	ReplayOf      string          `json:"replayOf,omitempty"`
}

// Sample is one line of timeline.jsonl: the state every 30 frames.
type Sample struct {
	Frame    uint64                     `json:"f"`
	Time     float64                    `json:"t"`
	Scene    string                     `json:"scene"`
	From     string                     `json:"from,omitempty"` // mid-transition: the scene still on screen
	Entities int                        `json:"entities"`
	Session  map[string]json.RawMessage `json:"session"`
}

// Play is one recorded play.
type Play struct {
	ID   string
	Dir  string
	Meta Meta

	times    []float64 // Times, read once
	timesErr error
}

// Root is a project's plays folder.
func Root(projectRoot string) string { return filepath.Join(projectRoot, filepath.FromSlash(Folder)) }

// List is every play in the project, newest first.
func List(projectRoot string) ([]*Play, error) {
	entries, err := os.ReadDir(Root(projectRoot))
	if os.IsNotExist(err) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	var out []*Play
	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		if p, err := Load(filepath.Join(Root(projectRoot), e.Name())); err == nil {
			out = append(out, p)
		}
	}
	// Newest first by when they started; one second's plays (id, id_2, id_10)
	// by their suffix. Ids are local time: a clock change would misorder them.
	sort.Slice(out, func(i, j int) bool {
		a, b := out[i], out[j]
		if a.Meta.Started != b.Meta.Started {
			return a.Meta.Started > b.Meta.Started
		}
		if len(a.ID) != len(b.ID) {
			return len(a.ID) > len(b.ID)
		}
		return a.ID > b.ID
	})
	return out, nil
}

// Load reads the play in dir.
func Load(dir string) (*Play, error) {
	data, err := os.ReadFile(filepath.Join(dir, "session.json"))
	if err != nil {
		return nil, fmt.Errorf("%s isn't a recorded play: %w", dir, err)
	}
	p := &Play{ID: filepath.Base(dir), Dir: dir}
	if err := json.Unmarshal(data, &p.Meta); err != nil {
		return nil, fmt.Errorf("%s/session.json: %w", dir, err)
	}
	return p, nil
}

// Find resolves a play by reference: an id (or a unique prefix of one),
// "latest" (or "" ), or "latest-N" for the one N before it.
func Find(projectRoot, ref string) (*Play, error) {
	all, err := List(projectRoot)
	if err != nil {
		return nil, err
	}
	if len(all) == 0 {
		return nil, fmt.Errorf("no recorded plays in %s: `jm run` records one each time you play", Folder)
	}
	if ref == "" || ref == "latest" {
		return all[0], nil
	}
	if n, ok := strings.CutPrefix(ref, "latest-"); ok {
		back, err := strconv.Atoi(n)
		if err != nil || back < 0 {
			return nil, fmt.Errorf("%q isn't a play: latest-N counts back from the latest", ref)
		}
		if back >= len(all) {
			return nil, fmt.Errorf("only %d plays recorded", len(all))
		}
		return all[back], nil
	}
	// A whole id: that play, or why its folder isn't one (List skipped it).
	if dir := filepath.Join(Root(projectRoot), ref); filepath.Base(ref) == ref {
		if _, err := os.Stat(dir); err == nil {
			return Load(dir)
		}
	}
	var matches []*Play
	for _, p := range all {
		if strings.HasPrefix(p.ID, ref) {
			matches = append(matches, p)
		}
	}
	switch len(matches) {
	case 1:
		return matches[0], nil
	case 0:
		return nil, fmt.Errorf("no play %q (jm plays lists them; latest is %s)", ref, all[0].ID)
	default:
		return nil, fmt.Errorf("%q matches %d plays; give more of the id", ref, len(matches))
	}
}

// Times is when each frame started, in seconds from the start (frames.bin
// summed): Times()[n] is frame n's time; one more entry ends the last frame.
func (p *Play) Times() ([]float64, error) {
	if p.times == nil && p.timesErr == nil {
		p.times, p.timesErr = p.readTimes()
	}
	return p.times, p.timesErr
}

// TimeOf is frame f's time, 0 when the play's timing can't be read.
func (p *Play) TimeOf(f uint64) float64 {
	times, _ := p.Times()
	if f < uint64(len(times)) {
		return times[f]
	}
	return 0
}

// Recorded is how many frames the play holds: frames.bin's count, which a
// play cut short (a crash) has fewer of than session.json says.
func (p *Play) Recorded() uint64 {
	times, _ := p.Times()
	if len(times) == 0 {
		return 0
	}
	return min(p.Meta.Frames, uint64(len(times)-1))
}

func (p *Play) readTimes() ([]float64, error) {
	data, err := os.ReadFile(filepath.Join(p.Dir, "frames.bin"))
	if err != nil {
		return nil, err
	}
	n := len(data) / 4
	times := make([]float64, n+1)
	for i := 0; i < n; i++ {
		dt := math.Float32frombits(binary.LittleEndian.Uint32(data[i*4:]))
		// A frame advances at most 0.1 s (a hitch): the engine records it capped;
		// older plays have the raw dt.
		times[i+1] = times[i] + math.Min(float64(dt), 0.1)
	}
	return times, nil
}

// Samples is the timeline: the state every 30 frames, and after the last.
func (p *Play) Samples() ([]Sample, error) {
	f, err := os.Open(filepath.Join(p.Dir, "timeline.jsonl"))
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var out []Sample
	scan := bufio.NewScanner(f)
	scan.Buffer(make([]byte, 1<<20), 1<<24)
	for scan.Scan() {
		var s Sample
		if json.Unmarshal(scan.Bytes(), &s) == nil {
			out = append(out, s)
		}
	}
	return out, nil
}

// Thumb is a small image of the game at a frame.
type Thumb struct {
	Frame uint64  `json:"frame"`
	Time  float64 `json:"time"`
	Path  string  `json:"path"` // absolute
}

// Thumbs is every thumbnail, in order.
func (p *Play) Thumbs() []Thumb {
	names, _ := filepath.Glob(filepath.Join(p.Dir, "thumbs", "*.jpg"))
	var out []Thumb
	for _, name := range names {
		frame, err := strconv.ParseUint(strings.TrimSuffix(filepath.Base(name), ".jpg"), 10, 64)
		if err != nil {
			continue
		}
		out = append(out, Thumb{Frame: frame, Time: p.TimeOf(frame), Path: name})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Frame < out[j].Frame })
	return out
}

// NearestThumb is the thumbnail closest to frame f; false when there are none.
func (p *Play) NearestThumb(f uint64) (Thumb, bool) {
	thumbs := p.Thumbs()
	if len(thumbs) == 0 {
		return Thumb{}, false
	}
	distance := func(t Thumb) uint64 { return max(t.Frame, f) - min(t.Frame, f) }
	best := thumbs[0]
	for _, t := range thumbs[1:] {
		if distance(t) < distance(best) {
			best = t
		}
	}
	return best, true
}

// CopyThumbs copies the thumbnails before frame f into another play's folder:
// a play resumed from f replays this one up to there without drawing it.
func (p *Play) CopyThumbs(dst string, f uint64) {
	for _, t := range p.Thumbs() {
		if t.Frame >= f {
			break
		}
		if data, err := os.ReadFile(t.Path); err == nil {
			_ = os.MkdirAll(filepath.Join(dst, "thumbs"), 0o755)
			_ = atomicfile.WriteFile(filepath.Join(dst, "thumbs", filepath.Base(t.Path)), data, 0o644)
		}
	}
}

var markerSpec = regexp.MustCompile(`^(marker:|m\d)`)
var timeSpec = regexp.MustCompile(`^(\d+(?:\.\d+)?)s$`)
var clockSpec = regexp.MustCompile(`^(\d+):(\d{1,2}(?:\.\d+)?)$`)

// FrameAt resolves a moment in the play to a frame: a frame number ("420"),
// a time ("12.5s", "1:05"), a marker ("marker:2", "m2"), "start" or "end".
func (p *Play) FrameAt(spec string) (uint64, error) {
	recorded := p.Recorded()
	if recorded == 0 {
		return 0, fmt.Errorf("play %s has no frames: the game ended (or crashed) before its first", p.ID)
	}
	last := recorded - 1
	spec = strings.TrimSpace(spec)
	inPlay := func(f uint64) (uint64, error) {
		if f > last {
			return 0, fmt.Errorf("frame %d is past the play's end (frame %d)", f, last)
		}
		return f, nil
	}
	switch {
	case spec == "" || spec == "end":
		return last, nil
	case spec == "start":
		return 0, nil
	case markerSpec.MatchString(spec):
		n, err := strconv.Atoi(strings.TrimPrefix(strings.TrimPrefix(spec, "marker:"), "m"))
		if err != nil {
			return 0, fmt.Errorf("bad marker %q (e.g. marker:2)", spec)
		}
		for _, m := range p.Meta.Markers {
			if m.N == n {
				return inPlay(m.Frame)
			}
		}
		return 0, fmt.Errorf("the play has %d marker(s); no marker %d", len(p.Meta.Markers), n)
	}
	seconds := -1.0
	if m := timeSpec.FindStringSubmatch(spec); m != nil {
		seconds, _ = strconv.ParseFloat(m[1], 64)
	} else if m := clockSpec.FindStringSubmatch(spec); m != nil {
		minutes, _ := strconv.ParseFloat(m[1], 64)
		secs, _ := strconv.ParseFloat(m[2], 64)
		if secs >= 60 {
			return 0, fmt.Errorf("%q isn't a time: a minute has 60 seconds (1:05, or 65s)", spec)
		}
		seconds = minutes*60 + secs
	}
	if seconds >= 0 {
		times, err := p.Times()
		if err != nil {
			return 0, err
		}
		// The frame running at that moment (dts are float32: their sums drift
		// by a few millionths, so "1s" is the frame starting at 1.00000005).
		// The play's own length ("10s" of a 10 s play) is its last frame.
		i := sort.Search(len(times), func(i int) bool { return times[i] > seconds+1e-4 })
		if i == 0 {
			return 0, nil
		}
		if f := uint64(i - 1); f >= recorded {
			if seconds <= times[len(times)-1]+1e-4 {
				return last, nil
			}
			return 0, fmt.Errorf("%s is past the play's end (it lasts %.2fs)", spec, times[len(times)-1])
		}
		return uint64(i - 1), nil
	}
	f, err := strconv.ParseUint(spec, 10, 64)
	if err != nil {
		return 0, fmt.Errorf("%q isn't a moment: give a frame (420), a time (12.5s or 1:05), a marker (marker:2), start or end", spec)
	}
	return inPlay(f)
}
