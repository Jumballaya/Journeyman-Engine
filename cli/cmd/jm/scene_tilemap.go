package main

import (
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"image"
	"image/color"
	"image/draw"
	"io"
	"math"
	"os"
	"path"
	"sort"
	"strings"
)

// Tiled's flags in a gid's top bits; the engine's TileGrid reads the same.
const (
	tileFlipH, tileFlipV, tileFlipD, tileRotate = 0x80000000, 0x40000000, 0x20000000, 0x10000000
	tileFlags                                   = tileFlipH | tileFlipV | tileFlipD | tileRotate
)

// pictureSideMax caps a tile map's picture, and any image read for one, on
// each side, in pixels: a larger one is left to its box.
const pictureSideMax = 4096

type tiledLayer struct {
	Type        string       `json:"type"`
	Visible     *bool        `json:"visible"`
	Opacity     *float64     `json:"opacity"`
	OffsetX     float64      `json:"offsetx"`
	OffsetY     float64      `json:"offsety"`
	Data        any          `json:"data"` // gids: an array, or base64 (a compressed layer isn't drawn)
	Encoding    string       `json:"encoding"`
	Compression string       `json:"compression"`
	Layers      []tiledLayer `json:"layers"`
	Properties  []struct {
		Name  string `json:"name"`
		Value any    `json:"value"`
	} `json:"properties"`
}

// gids is a tile layer's cells; none for one it can't read.
func (l tiledLayer) gids() []uint32 {
	switch data := l.Data.(type) {
	case []any:
		out := make([]uint32, len(data))
		for i, cell := range data {
			n, _ := cell.(float64)
			out[i] = uint32(n)
		}
		return out
	case string:
		raw, err := base64.StdEncoding.DecodeString(strings.TrimSpace(data))
		if l.Encoding != "base64" || l.Compression != "" || err != nil {
			return nil
		}
		out := make([]uint32, len(raw)/4)
		for i := range out {
			out[i] = binary.LittleEndian.Uint32(raw[4*i:])
		}
		return out
	}
	return nil
}

type tiledTileset struct {
	FirstGID   uint32 `json:"firstgid"`
	Source     string `json:"source"`
	TileWidth  int    `json:"tilewidth"`
	TileHeight int    `json:"tileheight"`
	TileOffset struct {
		X, Y int
	} `json:"tileoffset"`
	Image     string `json:"image"`
	Columns   int    `json:"columns"`
	TileCount int    `json:"tilecount"`
	Margin    int    `json:"margin"`
	Spacing   int    `json:"spacing"`
	Tiles     []struct {
		ID        int    `json:"id"`
		Image     string `json:"image"`
		X         int    `json:"x"`
		Y         int    `json:"y"`
		Width     int    `json:"width"`
		Height    int    `json:"height"`
		Animation []struct {
			TileID int `json:"tileid"`
		} `json:"animation"`
	} `json:"tiles"`
}

// tileMapPicture is a .tmj's tile layers drawn as the engine draws them, in
// file order, y down as an image (row 0 on top); nil for a map it can't read
// or one too large to send.
func tileMapPicture(root, file string) image.Image {
	var m struct {
		Width, Height int
		TileWidth     int `json:"tilewidth"`
		TileHeight    int `json:"tileheight"`
		Layers        []tiledLayer
		Tilesets      []tiledTileset
	}
	if !readGameJSON(root, file, &m) || m.Width <= 0 || m.Height <= 0 || m.TileWidth <= 0 || m.TileHeight <= 0 ||
		m.Width > pictureSideMax/m.TileWidth || m.Height > pictureSideMax/m.TileHeight {
		return nil
	}
	sets := []tilesetImages{}
	for _, ref := range m.Tilesets {
		set, dir := ref, path.Dir(file)
		if ref.Source != "" {
			source := path.Join(dir, ref.Source)
			if !readGameJSON(root, source, &set) {
				continue
			}
			set.FirstGID, dir = ref.FirstGID, path.Dir(source)
		}
		sets = append(sets, tilesetImages{set.FirstGID, set.cut(root, dir), image.Pt(set.TileOffset.X, set.TileOffset.Y)})
	}
	sort.Slice(sets, func(i, j int) bool { return sets[i].first < sets[j].first })
	tile := func(gid uint32) (image.Image, image.Point) {
		i := sort.Search(len(sets), func(i int) bool { return sets[i].first > gid }) - 1
		if i < 0 {
			return nil, image.Point{}
		}
		return sets[i].tiles[int(gid-sets[i].first)], sets[i].offset
	}

	// The tile layers in the engine's order: by their "z" property, else stacked
	// upward in file order (groups' layers in theirs), each with its group's
	// opacity and offset.
	type tileLayer struct {
		cells   []uint32
		opacity float64
		offset  image.Point
		z       float64
	}
	var layers []tileLayer
	depth := 0.0
	var collect func(l tiledLayer, shown bool, opacity, offsetX, offsetY float64)
	collect = func(l tiledLayer, shown bool, opacity, offsetX, offsetY float64) {
		z := depth // tile, object and image layers each take a step up, shown or not
		if l.Type == "tilelayer" || l.Type == "objectgroup" || l.Type == "imagelayer" {
			depth += 0.01
		}
		shown = shown && (l.Visible == nil || *l.Visible)
		if l.Opacity != nil {
			opacity *= *l.Opacity
		}
		offsetX, offsetY = offsetX+l.OffsetX, offsetY+l.OffsetY // rounded once, where it's drawn
		for _, child := range l.Layers {
			collect(child, shown, opacity, offsetX, offsetY)
		}
		if l.Type != "tilelayer" || !shown {
			return
		}
		for _, p := range l.Properties {
			if n, ok := p.Value.(float64); ok && p.Name == "z" {
				z = n
			}
		}
		cells := l.gids()
		cells = append(cells[:min(len(cells), m.Width*m.Height)], make([]uint32, max(0, m.Width*m.Height-len(cells)))...) // as the engine: short data padded
		layers = append(layers, tileLayer{cells, opacity, image.Pt(int(math.Round(offsetX)), int(math.Round(offsetY))), z})
	}
	for _, l := range m.Layers {
		collect(l, true, 1, 0, 0)
	}
	sort.SliceStable(layers, func(i, j int) bool { return layers[i].z < layers[j].z })

	canvas := image.NewNRGBA(image.Rect(0, 0, m.Width*m.TileWidth, m.Height*m.TileHeight))
	for _, l := range layers {
		mask := image.NewUniform(color.Alpha{uint8(255 * min(1, max(0, l.opacity)))})
		for row := m.Height - 1; row >= 0; row-- { // bottom row first, as the engine draws
			for col := range m.Width {
				gid := l.cells[row*m.Width+col]
				img, shift := tile(gid &^ tileFlags)
				if img == nil {
					continue
				}
				// A tile stands on its cell's bottom-left, as in Tiled; turned, it turns about its middle.
				size := img.Bounds().Size()
				at := image.Pt(col*m.TileWidth, (row+1)*m.TileHeight-size.Y).Add(l.offset).Add(shift)
				img = flipped(img, gid&tileFlipD != 0, gid&tileFlipH != 0, gid&tileFlipV != 0)
				b := img.Bounds()
				at = at.Add(image.Pt((size.X-b.Dx())/2, (size.Y-b.Dy())/2))
				draw.DrawMask(canvas, image.Rectangle{at, at.Add(b.Size())}, img, b.Min, mask, image.Point{}, draw.Over)
			}
		}
	}
	return canvas
}

type tilesetImages struct {
	first  uint32
	tiles  map[int]image.Image
	offset image.Point // the tileset's tileoffset
}

// cut is a tileset's tiles by id: a sheet cut into its grid, and tiles with
// images of their own (a collection, or a sheet's tile drawn otherwise).
func (t tiledTileset) cut(root, dir string) map[int]image.Image {
	tiles := map[int]image.Image{}
	if sheet := gameImage(root, path.Join(dir, t.Image)); t.Image != "" && sheet != nil && t.TileWidth > 0 && t.TileHeight > 0 &&
		max(t.TileWidth, t.TileHeight, t.Margin, t.Spacing) <= pictureSideMax && min(t.Margin, t.Spacing) >= 0 {
		// Only the cells the sheet has: its columns and rows bound the count.
		fits := func(side, tile int) int { return max(0, (side-2*t.Margin+t.Spacing)/(tile+t.Spacing)) }
		columns := min(max(1, t.Columns), fits(sheet.Bounds().Dx(), t.TileWidth))
		for id := range min(t.TileCount, columns*fits(sheet.Bounds().Dy(), t.TileHeight)) {
			at := image.Pt(t.Margin+id%columns*(t.TileWidth+t.Spacing), t.Margin+id/columns*(t.TileHeight+t.Spacing))
			tiles[id] = subImage(sheet, image.Rectangle{at, at.Add(image.Pt(t.TileWidth, t.TileHeight))})
		}
	}
	for _, spec := range t.Tiles {
		img := gameImage(root, path.Join(dir, spec.Image))
		if spec.Image == "" || img == nil {
			continue
		}
		if spec.Width > 0 && spec.Height > 0 {
			img = subImage(img, image.Rect(spec.X, spec.Y, spec.X+spec.Width, spec.Y+spec.Height))
		}
		tiles[spec.ID] = img
	}
	// An animated tile shows its first frame as the map starts.
	first := map[int]image.Image{}
	for _, spec := range t.Tiles {
		if len(spec.Animation) > 0 {
			first[spec.ID] = tiles[spec.Animation[0].TileID]
		}
	}
	for id, img := range first {
		tiles[id] = img
	}
	return tiles
}

func subImage(img image.Image, r image.Rectangle) image.Image {
	r = r.Add(img.Bounds().Min)
	if s, ok := img.(interface {
		SubImage(image.Rectangle) image.Image
	}); ok {
		return s.SubImage(r)
	}
	return img
}

// flipped is img as Tiled's flags turn it: across its diagonal (x and y
// swapped) first, then left to right, then top to bottom.
func flipped(img image.Image, d, h, v bool) image.Image {
	if !d && !h && !v {
		return img
	}
	b := img.Bounds()
	w, ht := b.Dx(), b.Dy()
	if d {
		w, ht = ht, w
	}
	out := image.NewNRGBA(image.Rect(0, 0, w, ht))
	for y := range ht {
		for x := range w {
			sx, sy := x, y
			if v {
				sy = ht - 1 - y
			}
			if h {
				sx = w - 1 - x
			}
			if d {
				sx, sy = sy, sx
			}
			out.Set(x, y, img.At(b.Min.X+sx, b.Min.Y+sy))
		}
	}
	return out
}

func readGameJSON(root, file string, v any) bool {
	real, outside := contentFile(root, file)
	if outside != nil {
		return false
	}
	data, err := os.ReadFile(real)
	return err == nil && json.Unmarshal(data, v) == nil
}

func readGameImage(root, file string) image.Image {
	real, outside := contentFile(root, file)
	if outside != nil {
		return nil
	}
	f, err := os.Open(real)
	if err != nil {
		return nil
	}
	defer f.Close()
	if size, _, err := image.DecodeConfig(f); err != nil || size.Width > pictureSideMax || size.Height > pictureSideMax {
		return nil
	}
	if _, err := f.Seek(0, io.SeekStart); err != nil {
		return nil
	}
	img, _, err := image.Decode(f)
	if err != nil {
		return nil
	}
	return img
}
