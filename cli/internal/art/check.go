package art

import (
	"encoding/json"
	"fmt"
	"image"
	_ "image/png"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
)

// Report is the game's art against its art note: each dossier's sprite, how
// far along it is, and what doesn't keep the locks.
type Report struct {
	Bible    *Bible    `json:"bible"` // none: the game has no art note
	Palette  []Color   `json:"palette"`
	Assets   []Asset   `json:"assets"`
	Findings []Finding `json:"findings"`
}

// Asset is a dossier's art: planned (no sprite yet), drawn (no sheet
// exported), exported, or done (its note says so, and nothing's wrong).
type Asset struct {
	Title    string `json:"title"`
	Note     string `json:"note"`
	Set      string `json:"set"` // its animation set, or its type
	Status   string `json:"status"`
	Sprite   string `json:"sprite,omitempty"`
	Sheet    string `json:"sheet,omitempty"`
	Image    string `json:"image,omitempty"` // the sheet's picture, and its first frame on it
	Frame    [4]int `json:"frame,omitempty"`
	Problems int    `json:"problems"`
}

type Finding struct {
	Level   string `json:"level"` // error or warning
	File    string `json:"file"`
	Message string `json:"message"`
}

// Check holds the game's art to its art note: a dossier with a sprite
// property, or of a type the note sizes, is an asset; its sheet (the
// sprite's .json export) keeps the palette, its size and its set's tags.
// Every atlas's images and tileset keep the palette and the grid.
func Check(root string) Report {
	r := Report{Assets: []Asset{}, Findings: []Finding{}, Palette: []Color{}}
	c := checker{root: root, report: &r, seen: map[string]bool{}}
	b, ok, err := Load(root)
	if ok {
		r.Bible = &b
	}
	if err != nil {
		c.add("error", b.Path, "the art note can't be read: "+err.Error())
	}
	if b.Palette != "" {
		if r.Palette, err = LoadPalette(c.path(b.Palette)); err != nil {
			c.add("error", b.Path, fmt.Sprintf("its palette %s can't be read: %v", b.Palette, err))
			r.Palette = []Color{}
		}
		for _, col := range r.Palette {
			c.colors = append(c.colors, col.Hex)
		}
		if b.Outline != "" && len(r.Palette) > 0 && !slices.ContainsFunc(r.Palette, func(col Color) bool { return col.Name == b.Outline || col.Hex == b.Outline }) {
			c.add("warning", b.Path, fmt.Sprintf("its outline %q isn't one of the palette's colours", b.Outline))
		}
	}
	for _, ref := range b.References {
		if !exists(c.path(ref)) {
			c.add("warning", b.Path, fmt.Sprintf("its reference %s isn't there", ref))
		}
	}
	c.bible = b
	listed := c.atlases()
	for _, n := range memory.Notes(root) {
		set, _ := n.Props["art_set"].(string)
		set = strings.TrimSpace(set)
		if set == "" {
			set = n.Type
		}
		sprite, _ := n.Props["sprite"].(string)
		_, sized := b.Sizes[set]
		_, animated := b.Sets[set]
		if sprite = strings.TrimSpace(sprite); sprite == "" && !sized && !animated || n.Type == "art" {
			continue
		}
		r.Assets = append(r.Assets, c.asset(n, set, sprite, listed))
	}
	return r
}

type checker struct {
	root   string
	bible  Bible
	colors []string // the palette's, #rrggbb; none: any colour
	report *Report
	seen   map[string]bool // images checked
}

func (c *checker) add(level, file, message string) {
	c.report.Findings = append(c.report.Findings, Finding{level, file, message})
}

// path is a game file's path on disk; one leading out of the game is none.
func (c *checker) path(rel string) string {
	rel = filepath.Clean(filepath.FromSlash(rel))
	if !filepath.IsLocal(rel) {
		return ""
	}
	return filepath.Join(c.root, rel)
}

func (c *checker) asset(n memory.Note, set, sprite string, listed map[string]bool) (a Asset) {
	a = Asset{Title: n.Title, Note: n.Path, Set: set, Status: "planned", Sprite: sprite}
	stated, _ := n.Props["art"].(string)
	before := len(c.report.Findings)
	defer func() { a.Problems = len(c.report.Findings) - before }()
	if sprite == "" || !exists(c.path(sprite)) {
		if sprite != "" {
			c.add("warning", n.Path, fmt.Sprintf("its sprite %s isn't there", sprite))
		}
		if stated == "done" {
			c.add("warning", n.Path, "art: done, but it has no sprite")
		}
		return a
	}
	a.Status, a.Sheet = "drawn", strings.TrimSuffix(sprite, filepath.Ext(sprite))+".json"
	if !exists(c.path(a.Sheet)) {
		a.Sheet = ""
		if stated == "done" {
			c.add("warning", n.Path, "art: done, but its sprite has no sheet exported beside it (.json, tags on)")
		}
		return a
	}
	a.Status = "exported"
	if !listed[filepath.ToSlash(filepath.Clean(a.Sheet))] {
		c.add("warning", a.Sheet, "no atlas lists it: the game can't show it")
	}
	a.Image, a.Frame = c.sheet(a.Sheet, set, n.Type)
	if stated == "done" && len(c.report.Findings) == before {
		a.Status = "done"
	}
	return a
}

// atlases checks every atlas's images, and every tileset's: the sheets the atlases list.
func (c *checker) atlases() map[string]bool {
	listed := map[string]bool{}
	_ = filepath.WalkDir(c.root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() {
			if name := d.Name(); path != c.root && (strings.HasPrefix(name, ".") || name == "build" || name == "node_modules" || name == memory.Dir) {
				return filepath.SkipDir
			}
			return nil
		}
		rel, _ := filepath.Rel(c.root, path)
		rel = filepath.ToSlash(rel)
		switch {
		case strings.HasSuffix(rel, ".atlas.json"):
			data, _ := os.ReadFile(path)
			config, err := atlas.LoadConfig(data)
			if err != nil {
				return nil // the build says so
			}
			for _, src := range config.Sources {
				src = filepath.ToSlash(filepath.Clean(src))
				listed[src] = true
				if strings.HasSuffix(src, ".json") {
					c.sheet(src, "", "")
				} else {
					c.image(src, nil)
				}
			}
		case strings.HasSuffix(rel, ".tsj"):
			c.tileset(rel)
		}
		return nil
	})
	return listed
}

// sheet checks an Aseprite sheet: its picture's colours, and (for a set
// or type the note has) its frames' size and its tags. Its picture and
// first frame, for a thumbnail.
func (c *checker) sheet(rel, set, kind string) (string, [4]int) {
	data, err := os.ReadFile(c.path(rel))
	if err != nil {
		return "", [4]int{}
	}
	s, err := atlas.ParseSheet(data)
	if err != nil {
		c.add("warning", rel, err.Error())
		return "", [4]int{}
	}
	img := filepath.ToSlash(filepath.Join(filepath.Dir(rel), s.Image))
	frames := s.Frames()
	c.image(img, frames)
	if set == "" && kind == "" || len(frames) == 0 {
		return img, [4]int{}
	}
	if w, h := c.bible.Size(set, kind); w > 0 {
		for _, f := range frames {
			if f.SourceW != w || f.SourceH != h {
				c.add("warning", rel, fmt.Sprintf("its frames are %dx%d; %s art is %dx%d", f.SourceW, f.SourceH, set, w, h))
				break
			}
		}
	}
	first := frames[0]
	if want, ok := c.bible.Sets[set]; ok {
		have := map[string]atlas.SheetTag{}
		for _, t := range s.Tags() {
			have[t.Name] = t
		}
		tags := want.Tags()
		var missing []string
		for _, name := range sortedKeys(tags) {
			t, ok := have[name]
			if !ok {
				missing = append(missing, name)
				continue
			}
			if name == "idle_down" || name == "idle" {
				first = frames[t.From]
			}
			c.timing(rel, name, frames[t.From:t.To+1], tags[name])
		}
		if len(missing) > 0 {
			c.add("warning", rel, fmt.Sprintf("the %s set's tags it lacks: %s", set, strings.Join(missing, ", ")))
		}
	}
	return img, [4]int{first.X, first.Y, first.W, first.H}
}

// timing checks a tag's frames against its action's.
func (c *checker) timing(rel, tag string, frames []atlas.SheetFrame, want Timing) {
	if len(frames) != len(want) {
		c.add("warning", rel, fmt.Sprintf("%s has %d frames; the set says %d", tag, len(frames), len(want)))
		return
	}
	for i, ms := range want {
		if ms > 0 && frames[i].Milliseconds != ms {
			c.add("warning", rel, fmt.Sprintf("%s's frame %d is %d ms; the set says %d", tag, i+1, frames[i].Milliseconds, ms))
			return
		}
	}
}

// tileset checks a Tiled tileset: its tiles keep the grid, its pictures the palette.
func (c *checker) tileset(rel string) {
	data, err := os.ReadFile(c.path(rel))
	if err != nil {
		return
	}
	var ts struct {
		TileWidth  int    `json:"tilewidth"`
		TileHeight int    `json:"tileheight"`
		Image      string `json:"image"`
		Tiles      []struct {
			Image string `json:"image"`
		} `json:"tiles"`
	}
	if json.Unmarshal(data, &ts) != nil {
		return // the build says so
	}
	if g := c.bible.Grid; g > 0 && (ts.TileWidth%g != 0 || ts.TileHeight%g != 0) {
		c.add("warning", rel, fmt.Sprintf("its tiles are %dx%d, off the %d px grid", ts.TileWidth, ts.TileHeight, g))
	}
	images := []string{ts.Image}
	for _, t := range ts.Tiles {
		images = append(images, t.Image)
	}
	for _, img := range images {
		if img != "" {
			c.image(filepath.ToSlash(filepath.Join(filepath.Dir(rel), img)), nil)
		}
	}
}

// image checks a picture's colours, once: the palette's, opaque or clear, and
// (in frames, when given) no more of them a frame than the note allows.
func (c *checker) image(rel string, frames []atlas.SheetFrame) {
	rel = filepath.ToSlash(filepath.Clean(rel))
	if c.seen[rel] || len(c.colors) == 0 && c.bible.Colors == 0 {
		return
	}
	c.seen[rel] = true
	f, err := os.Open(c.path(rel))
	if err != nil {
		return
	}
	defer f.Close()
	img, _, err := image.Decode(f)
	if err != nil {
		c.add("warning", rel, "it isn't a picture jm can read: "+err.Error())
		return
	}
	off, soft := 0, 0
	var where image.Point
	var first string
	b := img.Bounds()
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			hex, alpha := hexAt(img, x, y)
			switch {
			case alpha == 0:
			case alpha < 255:
				soft++
			case len(c.colors) > 0 && !slices.Contains(c.colors, hex):
				if off++; off == 1 {
					where, first = image.Pt(x, y), hex
				}
			}
		}
	}
	if off > 0 {
		c.add("warning", rel, fmt.Sprintf("%d pixels off the palette, the first %s at %d,%d", off, first, where.X, where.Y))
	}
	if soft > 0 {
		c.add("warning", rel, fmt.Sprintf("%d half-clear pixels: a pixel is opaque or clear", soft))
	}
	if c.bible.Colors <= 0 {
		return
	}
	if frames == nil {
		frames = []atlas.SheetFrame{{X: b.Min.X, Y: b.Min.Y, W: b.Dx(), H: b.Dy()}}
	}
	for i, fr := range frames {
		used := map[string]bool{}
		for y := fr.Y; y < fr.Y+fr.H; y++ {
			for x := fr.X; x < fr.X+fr.W; x++ {
				if hex, alpha := hexAt(img, x, y); alpha == 255 {
					used[hex] = true
				}
			}
		}
		if len(used) > c.bible.Colors {
			c.add("warning", rel, fmt.Sprintf("frame %d uses %d colours; the art note allows %d", i+1, len(used), c.bible.Colors))
			return
		}
	}
}

func hexAt(img image.Image, x, y int) (string, int) {
	r, g, b, a := img.At(x, y).RGBA()
	if a == 0 {
		return "", 0
	}
	if a < 0xffff {
		return "", int(a >> 8)
	}
	return fmt.Sprintf("#%02x%02x%02x", r>>8, g>>8, b>>8), 255
}

func exists(path string) bool {
	info, err := os.Stat(path)
	return path != "" && err == nil && info.Mode().IsRegular()
}

func sortedKeys[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	slices.Sort(keys)
	return keys
}
