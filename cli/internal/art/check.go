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
	c := checker{root: root, checked: map[string][]Finding{}, decoded: map[string]image.Image{}}
	b, ok, err := Load(root)
	if ok {
		r.Bible = &b
	}
	var own []Finding // the note's
	say := func(level, message string) { own = append(own, Finding{level, b.Path, message}) }
	if err != nil {
		say("error", "the art note can't be read: "+err.Error())
	}
	if b.Palette != "" {
		if r.Palette, err = LoadPalette(c.path(b.Palette)); err != nil {
			say("error", fmt.Sprintf("its palette %s can't be read: %v", b.Palette, err))
			r.Palette = []Color{}
		}
		for _, col := range r.Palette {
			c.colors = append(c.colors, col.Hex)
		}
		if b.Outline != "" && len(r.Palette) > 0 && !slices.ContainsFunc(r.Palette, func(col Color) bool { return col.Name == b.Outline || col.Hex == b.Outline }) {
			say("warning", fmt.Sprintf("its outline %q isn't one of the palette's colours", b.Outline))
		}
	}
	for _, ref := range b.References {
		if !exists(c.path(ref)) {
			say("warning", fmt.Sprintf("its reference %s isn't there", ref))
		}
	}
	c.bible = b
	listed, all := c.atlases()
	all = append([][]Finding{own}, all...)
	for _, n := range memory.Notes(root) {
		set, _ := n.Props["art_set"].(string)
		if set = strings.TrimSpace(set); set == "" {
			set = n.Type
		}
		sprite, _ := n.Props["sprite"].(string)
		_, sized := b.Sizes[set]
		_, animated := b.Sets[set]
		if sprite = strings.TrimSpace(sprite); sprite == "" && !sized && !animated || n.Type == "art" {
			continue
		}
		a, found := c.asset(n, set, sprite, listed)
		r.Assets = append(r.Assets, a)
		all = append(all, found)
	}
	seen := map[Finding]bool{} // once each: an atlas and a dossier may share a sheet
	for _, f := range slices.Concat(all...) {
		if !seen[f] {
			seen[f] = true
			r.Findings = append(r.Findings, f)
		}
	}
	return r
}

type checker struct {
	root    string
	bible   Bible
	colors  []string               // the palette's, #rrggbb; none: any colour
	checked map[string][]Finding   // by what was checked: each once
	decoded map[string]image.Image // pictures read, nil for one that can't be
}

func (c *checker) path(rel string) string { return gamePath(c.root, rel) }

// gamePath is a game file's path on disk; one leading out of the game (by
// .. or a link) is none.
func gamePath(root, rel string) string {
	rel = filepath.Clean(filepath.FromSlash(rel))
	if !filepath.IsLocal(rel) {
		return ""
	}
	path := filepath.Join(root, rel)
	realRoot, err := filepath.EvalSymlinks(root)
	if err != nil {
		return ""
	}
	real, err := filepath.EvalSymlinks(path)
	if err != nil {
		return path // not there: reading it fails
	}
	if within, err := filepath.Rel(realRoot, real); err != nil || !filepath.IsLocal(within) {
		return ""
	}
	return real
}

// once is check's findings for key, checked the first time it's asked.
func (c *checker) once(key string, check func(say func(file, message string))) []Finding {
	if found, ok := c.checked[key]; ok {
		return found
	}
	found := []Finding{}
	check(func(file, message string) { found = append(found, Finding{"warning", file, message}) })
	c.checked[key] = found
	return found
}

func (c *checker) asset(n memory.Note, set, sprite string, listed map[string]bool) (a Asset, found []Finding) {
	a = Asset{Title: n.Title, Note: n.Path, Set: set, Status: "planned", Sprite: sprite}
	stated, _ := n.Props["art"].(string)
	say := func(file, message string) { found = append(found, Finding{"warning", file, message}) }
	defer func() { a.Problems = len(found) }()
	sheet := filepath.ToSlash(filepath.Clean(strings.TrimSuffix(sprite, filepath.Ext(sprite)) + ".json"))
	switch {
	case sprite == "" || !exists(c.path(sprite)):
		if sprite != "" {
			say(n.Path, fmt.Sprintf("its sprite %s isn't there", sprite))
		}
		if stated == "done" {
			say(n.Path, "art: done, but it has no sprite")
		}
	case !exists(c.path(sheet)):
		a.Status = "drawn"
		if stated == "done" {
			say(n.Path, "art: done, but its sprite has no sheet exported beside it (.json, tags on)")
		}
	default:
		a.Status, a.Sheet = "exported", sheet
		if !listed[sheet] {
			say(sheet, "no atlas lists it: the game can't show it")
		}
		var problems []Finding
		a.Image, a.Frame, problems = c.sheet(sheet, set, n.Type)
		if found = append(found, problems...); stated == "done" && len(found) == 0 {
			a.Status = "done"
		}
	}
	return a, found
}

// atlases checks every atlas's images, and every tileset's: the sheets the
// atlases list, and the findings.
func (c *checker) atlases() (map[string]bool, [][]Finding) {
	listed := map[string]bool{}
	var found [][]Finding
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
			data, _ := os.ReadFile(c.path(rel))
			config, err := atlas.LoadConfig(data)
			if err != nil {
				return nil // the build says so
			}
			for _, src := range config.Sources {
				src = filepath.ToSlash(filepath.Clean(src))
				listed[src] = true
				if strings.HasSuffix(src, ".json") {
					_, _, sheet := c.sheet(src, "", "")
					found = append(found, sheet)
				} else {
					found = append(found, c.image(src, nil))
				}
			}
		case strings.HasSuffix(rel, ".tsj"):
			found = append(found, c.tileset(rel))
		}
		return nil
	})
	return listed, found
}

// sheet checks an Aseprite sheet: its picture's colours, and (for a set or
// type the note has) its frames' size and its tags. Its picture and first
// frame, for a thumbnail.
func (c *checker) sheet(rel, set, kind string) (img string, first [4]int, found []Finding) {
	data, err := os.ReadFile(c.path(rel))
	if err != nil {
		return "", first, nil
	}
	s, err := atlas.ParseSheet(data)
	if err != nil {
		return "", first, c.once(rel, func(say func(string, string)) { say(rel, err.Error()) })
	}
	img = filepath.ToSlash(filepath.Join(filepath.Dir(rel), s.Image))
	frames := s.Frames()
	if len(frames) > 0 {
		first = [4]int{frames[0].X, frames[0].Y, frames[0].W, frames[0].H}
	}
	found = c.image(img, frames)
	if set == "" && kind == "" {
		return img, first, found
	}
	return img, first, append(found, c.once(rel+"#"+set+"#"+kind, func(say func(string, string)) {
		if w, h := c.bible.Size(set, kind); w > 0 {
			if i := slices.IndexFunc(frames, func(f atlas.SheetFrame) bool { return f.SourceW != w || f.SourceH != h }); i >= 0 {
				say(rel, fmt.Sprintf("its frames are %dx%d; %s art is %dx%d", frames[i].SourceW, frames[i].SourceH, set, w, h))
			}
		}
		want, ok := c.bible.Sets[set]
		if !ok {
			return
		}
		have := map[string]atlas.SheetTag{}
		for _, t := range s.Tags() {
			have[t.Name] = t
		}
		tags := want.Tags()
		var missing []string
		for _, name := range sortedKeys(tags) {
			if t, ok := have[name]; !ok {
				missing = append(missing, name)
			} else if off := timing(name, frames[t.From:t.To+1], tags[name]); off != "" {
				say(rel, off)
			}
		}
		if len(missing) > 0 {
			say(rel, fmt.Sprintf("the %s set's tags it lacks: %s", set, strings.Join(missing, ", ")))
		}
	})...)
}

// timing is what's off a tag's frames against its action's: "" when nothing.
func timing(tag string, frames []atlas.SheetFrame, want Timing) string {
	if len(frames) != len(want) {
		return fmt.Sprintf("%s has %d frames; the set says %d", tag, len(frames), len(want))
	}
	for i, ms := range want {
		if ms > 0 && frames[i].Milliseconds != ms {
			return fmt.Sprintf("%s's frame %d is %d ms; the set says %d", tag, i+1, frames[i].Milliseconds, ms)
		}
	}
	return ""
}

// tileset checks a Tiled tileset: its tiles keep the grid; its pictures
// the palette, and the colours a tile.
func (c *checker) tileset(rel string) []Finding {
	return c.once(rel, func(say func(string, string)) {
		data, err := os.ReadFile(c.path(rel))
		if err != nil {
			return
		}
		var ts struct {
			TileWidth  int    `json:"tilewidth"`
			TileHeight int    `json:"tileheight"`
			Margin     int    `json:"margin"`
			Spacing    int    `json:"spacing"`
			Image      string `json:"image"`
			Tiles      []struct {
				Image string `json:"image"`
			} `json:"tiles"`
		}
		if json.Unmarshal(data, &ts) != nil {
			return // the build says so
		}
		if g := c.bible.Grid; g > 0 && (ts.TileWidth%g != 0 || ts.TileHeight%g != 0) {
			say(rel, fmt.Sprintf("its tiles are %dx%d, off the %d px grid", ts.TileWidth, ts.TileHeight, g))
		}
		var found []Finding
		if ts.Image != "" { // one sheet: a frame a tile
			img := filepath.ToSlash(filepath.Join(filepath.Dir(rel), ts.Image))
			found = c.image(img, c.cells(img, ts.TileWidth, ts.TileHeight, ts.Margin, ts.Spacing))
		}
		for _, t := range ts.Tiles {
			if t.Image != "" {
				found = append(found, c.image(filepath.ToSlash(filepath.Join(filepath.Dir(rel), t.Image)), nil)...)
			}
		}
		for _, f := range found {
			say(f.File, f.Message)
		}
	})
}

// cells are a tileset sheet's tiles, as frames.
func (c *checker) cells(rel string, w, h, margin, spacing int) []atlas.SheetFrame {
	img := c.decode(rel)
	if img == nil || w <= 0 || h <= 0 || c.bible.Colors <= 0 {
		return nil
	}
	margin, spacing = max(margin, 0), max(spacing, 0)
	var out []atlas.SheetFrame
	b := img.Bounds()
	for y := margin; y+h <= b.Dy(); y += h + spacing {
		for x := margin; x+w <= b.Dx(); x += w + spacing {
			out = append(out, atlas.SheetFrame{X: x, Y: y, W: w, H: h})
		}
	}
	return out
}

// image checks a picture, once: the palette's colours, opaque or clear, and
// no more colours a frame (the whole picture, without frames) than the note allows.
func (c *checker) image(rel string, frames []atlas.SheetFrame) []Finding {
	rel = filepath.ToSlash(filepath.Clean(rel))
	key := rel // and its frames: two sheets may cut one picture
	if frames != nil {
		key += fmt.Sprint(frames)
	}
	return c.once(key, func(say func(string, string)) {
		if len(c.colors) == 0 && c.bible.Colors <= 0 {
			if !exists(c.path(rel)) {
				say(rel, "it isn't there")
			}
			return
		}
		img := c.decode(rel)
		if img == nil {
			say(rel, "it isn't there, or isn't a PNG jm can read")
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
						where, first = image.Pt(x-b.Min.X, y-b.Min.Y), hex
					}
				}
			}
		}
		if off > 0 {
			say(rel, fmt.Sprintf("%d pixels off the palette, the first %s at %d,%d", off, first, where.X, where.Y))
		}
		if soft > 0 {
			say(rel, fmt.Sprintf("%d half-clear pixels: a pixel is opaque or clear", soft))
		}
		if c.bible.Colors <= 0 {
			return
		}
		if frames == nil {
			frames = []atlas.SheetFrame{{W: b.Dx(), H: b.Dy()}}
		}
		for i, fr := range frames {
			r := image.Rect(fr.X, fr.Y, fr.X+fr.W, fr.Y+fr.H).Add(b.Min).Intersect(b) // a frame off the picture: the build says so
			used := map[string]bool{}
			for y := r.Min.Y; y < r.Max.Y; y++ {
				for x := r.Min.X; x < r.Max.X; x++ {
					if hex, alpha := hexAt(img, x, y); alpha == 255 {
						used[hex] = true
					}
				}
			}
			if len(used) > c.bible.Colors {
				say(rel, fmt.Sprintf("frame %d uses %d colours; the art note allows %d", i+1, len(used), c.bible.Colors))
				return
			}
		}
	})
}

// decode is a game picture, nil when it can't be read.
func (c *checker) decode(rel string) image.Image {
	if img, ok := c.decoded[rel]; ok {
		return img
	}
	var img image.Image
	if f, err := os.Open(c.path(rel)); err == nil {
		img, _, _ = image.Decode(f)
		f.Close()
	}
	c.decoded[rel] = img
	return img
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
