// Package art holds a game to its art direction: the locks a design note
// of type art states (palette, grid, sizes, animation sets), and checks
// of the exported art against them, sprite by sprite.
package art

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
	"gopkg.in/yaml.v3"
)

// Note is where a game's art direction starts (jm memory init writes it);
// any design note of type art is read.
const Note = memory.Dir + "/Art.md"

// Bible is a game's art direction, as its art note's properties state it.
type Bible struct {
	Path    string            `json:"path" yaml:"-"`
	Palette string            `json:"palette,omitempty" yaml:"palette"` // a .gpl, .hex or .pal file of the game
	Grid    int               `json:"grid,omitempty" yaml:"grid"`       // tiles and props are multiples of it
	View    string            `json:"view,omitempty" yaml:"view"`
	Light   string            `json:"light,omitempty" yaml:"light"`
	Outline string            `json:"outline,omitempty" yaml:"outline"` // a palette colour's name
	Colors  int               `json:"colors,omitempty" yaml:"colors"`   // the most colours one sprite uses (0: any)
	Sizes   map[string]string `json:"sizes,omitempty" yaml:"sizes"`     // a set or dossier type's frame: 32x32
	// Finished sprites (.aseprite or .png) a new one matches: its proportions, shading, outline.
	References []string       `json:"references,omitempty" yaml:"references"`
	Sets       map[string]Set `json:"animations,omitempty" yaml:"animations"`
}

// Set is a kind of sprite's animations: each action's frames (their
// milliseconds) drawn facing each direction, as <action>_<direction> tags.
type Set struct {
	Directions []string          `json:"directions,omitempty" yaml:"directions"`
	Undirected []string          `json:"undirected,omitempty" yaml:"undirected"` // actions drawn once, tagged by their name
	Actions    map[string]Timing `json:"actions" yaml:",inline"`
}

// Timing is an action's frames' milliseconds; 0 is any. A note may give a
// frame count instead (idle: 2).
type Timing []int

func (t *Timing) UnmarshalYAML(n *yaml.Node) error {
	if n.Kind == yaml.ScalarNode {
		count, err := strconv.Atoi(n.Value)
		if err != nil || count < 1 || count > 256 {
			return fmt.Errorf("line %d: an action is its frames' milliseconds ([150, 150]) or a frame count, not %q", n.Line, n.Value)
		}
		*t = make(Timing, count)
		return nil
	}
	var ms []int
	if err := n.Decode(&ms); err != nil {
		return fmt.Errorf("line %d: an action is its frames' milliseconds ([150, 150]) or a frame count", n.Line)
	}
	*t = ms
	return nil
}

// Tags are the tags a set wants, sorted: each action facing each direction.
func (s Set) Tags() map[string]Timing {
	out := map[string]Timing{}
	for action, t := range s.Actions {
		if len(s.Directions) == 0 || slices.Contains(s.Undirected, action) {
			out[action] = t
			continue
		}
		for _, d := range s.Directions {
			out[action+"_"+d] = t
		}
	}
	return out
}

// Load reads the game's art note: false when it has none.
func Load(root string) (Bible, bool, error) {
	path := ""
	for _, n := range memory.Notes(root) {
		if n.Type == "art" && (path == "" || n.Path == Note) {
			path = n.Path
		}
	}
	if path == "" {
		return Bible{}, false, nil
	}
	b := Bible{Path: path}
	data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(path)))
	if err != nil {
		return b, true, err
	}
	front, ok := strings.CutPrefix(strings.ReplaceAll(string(data), "\r\n", "\n"), "---\n")
	if front, _, ok = strings.Cut(front, "\n---"); !ok {
		return b, true, errors.New("it has no properties")
	}
	if err := yaml.Unmarshal([]byte(front), &b); err != nil {
		return b, true, fmt.Errorf("its properties: %w", err)
	}
	b.Path = path
	return b, true, nil
}

// Size is the frame a set or dossier type is drawn in: 0, 0 when the note gives none.
func (b Bible) Size(kinds ...string) (w, h int) {
	for _, k := range kinds {
		if s, ok := b.Sizes[k]; ok {
			ws, hs, _ := strings.Cut(strings.NewReplacer("×", "x", " ", "").Replace(strings.ToLower(s)), "x")
			w, _ = strconv.Atoi(ws)
			if h, _ = strconv.Atoi(hs); hs == "" {
				h = w
			}
			return w, h
		}
	}
	return 0, 0
}

// Color is one of a palette's colours, and its name if the file gives one.
type Color struct {
	Hex  string `json:"hex"` // #rrggbb
	Name string `json:"name,omitempty"`
}

// LoadPalette reads a GIMP (.gpl), JASC (.pal) or hex-per-line (.hex) palette.
func LoadPalette(path string) ([]Color, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var out []Color
	lines := bufio.NewScanner(bytes.NewReader(data))
	for i := 1; lines.Scan(); i++ {
		fields := strings.Fields(lines.Text())
		if len(fields) == 1 { // .hex's colours, or a header (JASC-PAL, 0100, 16)
			if hex := strings.TrimPrefix(fields[0], "#"); len(hex) == 6 {
				if _, err := strconv.ParseUint(hex, 16, 32); err == nil {
					out = append(out, Color{Hex: "#" + strings.ToLower(hex)})
				}
			}
			continue
		}
		if len(fields) < 3 || strings.HasPrefix(fields[0], "#") || strings.HasPrefix(fields[0], ";") || strings.HasSuffix(fields[0], ":") {
			continue // a comment or header (GIMP Palette, Name: ...)
		}
		var rgb [3]int
		for c := range rgb {
			if rgb[c], err = strconv.Atoi(fields[c]); err != nil || rgb[c] < 0 || rgb[c] > 255 {
				return nil, fmt.Errorf("line %d isn't a colour: %q", i, lines.Text())
			}
		}
		name := fields[3:]
		if len(name) > 1 && strings.HasPrefix(name[len(name)-1], "#") { // "gold-1 #341c27"
			name = name[:len(name)-1]
		}
		out = append(out, Color{Hex: fmt.Sprintf("#%02x%02x%02x", rgb[0], rgb[1], rgb[2]), Name: strings.Join(name, " ")})
	}
	if len(out) == 0 {
		return nil, errors.New("it has no colours")
	}
	return out, nil
}
