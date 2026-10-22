package art

import (
	"bytes"
	"cmp"
	"encoding/json"
	"fmt"
	"image"
	_ "image/png"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
	"github.com/Jumballaya/Journeyman-Engine/internal/realpath"
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
	c := checker{root: root, checked: map[string][]Finding{}}
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
		if data, err := c.read(b.Palette); err != nil {
			say("error", fmt.Sprintf("its palette %s can't be read: %v", b.Palette, err))
		} else if r.Palette, err = ParsePalette(data); err != nil {
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
		locked := func(k string) bool { _, sized := b.Sizes[k]; _, animated := b.Sets[k]; return sized || animated }
		if sprite = strings.TrimSpace(sprite); sprite == "" && !locked(set) && !locked(n.Type) || n.Type == "art" {
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
	colors  []string             // the palette's, #rrggbb; none: any colour
	checked map[string][]Finding // by what was checked: each once
}

func (c *checker) path(rel string) string { return gamePath(c.root, rel) }

func (c *checker) read(rel string) ([]byte, error) { return readGame(c.root, rel) }

// readGame reads a game file: a regular file in the game (not a pipe, nor
// through a link out of it).
func readGame(root, rel string) ([]byte, error) {
	path := gamePath(root, rel)
	if !exists(path) {
		return nil, fmt.Errorf("%s isn't a file in the game", rel)
	}
	return os.ReadFile(path)
}

// gamePath is a game file's path on disk; one leading out of the game (by
// .. or a link) is none.
func gamePath(root, rel string) string {
	real, _, _ := realpath.Rel(root, filepath.Clean(filepath.FromSlash(rel)))
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
		if found = append(found, c.sheet(sheet, set, n.Type)...); stated == "done" && len(found) == 0 {
			a.Status = "done"
		}
	}
	return a, found
}

// atlases checks the atlases and tilesets the game ships (its manifest's
// assets): their pictures, and the sheets the atlases list.
func (c *checker) atlases() (map[string]bool, [][]Finding) {
	listed := map[string]bool{}
	var found [][]Finding
	data, err := c.read(archive.ManifestEntryKey)
	man, err2 := manifest.LoadManifestFromBytes(data)
	if err != nil || err2 != nil {
		return listed, nil // the build says so
	}
	assets, _ := manifest.ExpandAssets(os.DirFS(c.root), man.Assets)
	for _, rel := range assets {
		switch {
		case strings.HasSuffix(rel, ".atlas.json"):
			data, _ := c.read(rel)
			config, err := atlas.LoadConfig(data)
			if err != nil {
				continue // the build says so
			}
			for _, src := range config.Sources {
				src = filepath.ToSlash(filepath.Clean(src))
				listed[src] = true
				if atlas.SourceOf(src).Sheet {
					found = append(found, c.sheet(src, "", ""))
				} else {
					found = append(found, c.image(src))
				}
			}
		case strings.HasSuffix(rel, ".tsj"):
			found = append(found, c.tileset(rel))
		}
	}
	return listed, found
}

// sheet checks an Aseprite sheet: its picture's colours, and (for a set or
// type the note has) its frames' size and its tags.
func (c *checker) sheet(rel, set, kind string) []Finding {
	data, err := c.read(rel)
	s, err2 := atlas.ParseSheet(data)
	if err != nil || err2 != nil {
		return c.once(rel, func(say func(string, string)) { say(rel, cmp.Or(err, err2).Error()) })
	}
	img := filepath.ToSlash(filepath.Join(filepath.Dir(rel), s.Image))
	found := slices.Concat(c.image(img), c.once(rel, func(say func(string, string)) {
		if picture, err := c.read(img); err == nil {
			if size, _, err := image.DecodeConfig(bytes.NewReader(picture)); err == nil {
				if i := s.Outside(size.Width, size.Height); i >= 0 {
					say(rel, fmt.Sprintf("its frame %d is outside its %dx%d picture", i+1, size.Width, size.Height))
				}
			}
		}
	}))
	if set == "" && kind == "" {
		return found
	}
	frames := s.Frames()
	return append(found, c.once(rel+"#"+set+"#"+kind, func(say func(string, string)) {
		if w, h := c.bible.Size(set, kind); w > 0 {
			if i := slices.IndexFunc(frames, func(f atlas.SheetFrame) bool { return f.W != w || f.H != h }); i >= 0 {
				say(rel, fmt.Sprintf("its frames are %dx%d; %s art is %dx%d", frames[i].W, frames[i].H, set, w, h))
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

// tileset checks a Tiled tileset: its tiles keep the grid, its pictures the palette.
func (c *checker) tileset(rel string) []Finding {
	return c.once(rel, func(say func(string, string)) {
		data, err := c.read(rel)
		if err != nil {
			return
		}
		var ts struct {
			TileWidth  int    `json:"tilewidth"`
			TileHeight int    `json:"tileheight"`
			Image      string `json:"image"`
			Tiles      []struct {
				ID            int    `json:"id"`
				Image         string `json:"image"`
				Width, Height int    // a crop of its image; none: all of it
			} `json:"tiles"`
		}
		if json.Unmarshal(data, &ts) != nil {
			return // the build says so
		}
		offGrid := func(w, h int) bool { g := c.bible.Grid; return g > 0 && (w%g != 0 || h%g != 0) }
		if ts.Image != "" && offGrid(ts.TileWidth, ts.TileHeight) {
			say(rel, fmt.Sprintf("its tiles are %dx%d, off the %d px grid", ts.TileWidth, ts.TileHeight, c.bible.Grid))
		}
		images := []string{ts.Image}
		for _, t := range ts.Tiles { // a collection: each tile its own picture, as big as it is
			images = append(images, t.Image)
			w, h := t.Width, t.Height
			if t.Image != "" && (w == 0 || h == 0) {
				w, h = c.size(filepath.ToSlash(filepath.Join(filepath.Dir(rel), t.Image)))
			}
			if (w > 0 || h > 0) && offGrid(w, h) {
				say(rel, fmt.Sprintf("its tile %d is %dx%d, off the %d px grid", t.ID, w, h, c.bible.Grid))
			}
		}
		for _, img := range images {
			if img != "" {
				for _, f := range c.image(filepath.ToSlash(filepath.Join(filepath.Dir(rel), img))) {
					say(f.File, f.Message)
				}
			}
		}
	})
}

// size is a picture's width and height: 0, 0 when it can't be read (image says so).
func (c *checker) size(rel string) (int, int) {
	data, err := c.read(rel)
	if err != nil {
		return 0, 0
	}
	cfg, _, err := image.DecodeConfig(bytes.NewReader(data))
	if err != nil {
		return 0, 0
	}
	return cfg.Width, cfg.Height
}

// image checks a picture's colours, once: the palette's, opaque or clear.
func (c *checker) image(rel string) []Finding {
	rel = filepath.ToSlash(filepath.Clean(rel))
	return c.once(rel, func(say func(string, string)) {
		data, err := c.read(rel)
		if err != nil {
			say(rel, "it isn't there")
			return
		}
		img, _, err := image.Decode(bytes.NewReader(data))
		if err != nil || len(c.colors) == 0 {
			if err != nil {
				say(rel, "it isn't a PNG jm can read: "+err.Error())
			}
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
				case !slices.Contains(c.colors, hex):
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
	})
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
