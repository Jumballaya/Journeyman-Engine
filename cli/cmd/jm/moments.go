package main

import (
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// A moment is a marker the person saved with F8 after picking entities,
// points and areas: what they meant. Its picture for the agent and the
// timeline is the clean frame with each pick outlined (a point: a crosshair)
// and labeled with its number and name, drawn here from the picks' rects
// (the engine saves only the clean frame).

var momentInk = color.RGBA{0xFF, 0xD2, 0x3F, 0xFF} // the engine's own outline color, while held

// momentPicture is the marker's picture with its picks outlined; nil when
// it has none to read.
func momentPicture(p *plays.Play, m plays.Marker) image.Image {
	src := readImage(markerImage(p, m))
	if src == nil || len(m.Picks) == 0 {
		return src
	}
	b := src.Bounds()
	img := image.NewRGBA(image.Rect(0, 0, b.Dx(), b.Dy()))
	draw.Draw(img, img.Bounds(), src, b.Min, draw.Src)
	scale := max(1, b.Dx()/256) // a pixel of line and type per 256 px of frame
	game := gameArea(img)
	type marked struct {
		r     image.Rectangle // what a label goes beside
		lines []string
	}
	var labels []marked
	var taken []image.Rectangle // what labels keep off: the marks, then the labels placed
	for i, pick := range m.Picks {
		if len(pick.Rect) != 4 {
			continue
		}
		w, h := float64(b.Dx()), float64(b.Dy())
		r := image.Rect(int(pick.Rect[0]*w), int(pick.Rect[1]*h), int((pick.Rect[0]+pick.Rect[2])*w), int((pick.Rect[1]+pick.Rect[3])*h))
		if pick.Kind == "point" { // a crosshair in a box: a pin on the spot
			at, arm := r.Min, 8*scale
			for _, c := range []struct {
				ink   color.RGBA
				thick int
			}{{color.RGBA{0, 0, 0, 0xFF}, scale + 2}, {momentInk, scale}} {
				fill(img, image.Rect(at.X-arm, at.Y-c.thick/2, at.X+arm+1, at.Y+(c.thick+1)/2), c.ink)
				fill(img, image.Rect(at.X-c.thick/2, at.Y-arm, at.X+(c.thick+1)/2, at.Y+arm+1), c.ink)
			}
			r = image.Rect(at.X-2*scale, at.Y-2*scale, at.X+2*scale+1, at.Y+2*scale+1)
		}
		r = r.Inset(-scale).Intersect(img.Bounds()) // around it, not over its edge
		outline(img, r.Inset(-1), scale+2, color.RGBA{0, 0, 0, 0xFF})
		outline(img, r, scale, momentInk)
		if pick.Kind == "point" { // the label clear of the crosshair's arms
			r = r.Inset(-6 * scale)
		}
		taken = append(taken, r)
		labels = append(labels, marked{r, noteLines(i+1, pick)})
	}
	inGame := img.SubImage(game).(*image.RGBA) // a label wider than the game is cut at its edge
	for _, l := range labels {
		tag := tagPlace(game, l.r, labelSize(l.lines, scale), taken)
		taken = append(taken, tag)
		label(inGame, tag, l.lines, scale)
	}
	return img
}

// tagPlace is where a tag goes beside r: above it, under it, then further up;
// inside the game's area (never on a letterbox bar); the first place clear
// of what's taken, else the one least on it.
func tagPlace(game, r image.Rectangle, tag image.Point, taken []image.Rectangle) image.Rectangle {
	places := []image.Point{{r.Min.X, r.Min.Y - tag.Y}, {r.Min.X, r.Max.Y}, {r.Max.X - tag.X, r.Min.Y - tag.Y}, {r.Max.X - tag.X, r.Max.Y}}
	for y := r.Min.Y - 2*tag.Y; y > game.Min.Y-tag.Y; y -= tag.Y/2 + 1 {
		places = append(places, image.Pt(r.Min.X, y), image.Pt(r.Max.X-tag.X, y))
	}
	best, least := image.Rectangle{}, -1
	for _, at := range append(places, r.Min) {
		at.X = max(game.Min.X, min(at.X, game.Max.X-tag.X))
		at.Y = max(game.Min.Y, min(at.Y, game.Max.Y-tag.Y))
		t, on := image.Rectangle{at, at.Add(tag)}, 0
		for _, o := range taken {
			i := t.Intersect(o)
			on += i.Dx() * i.Dy()
		}
		if least < 0 || on < least {
			best, least = t, on
		}
		if on == 0 {
			break
		}
	}
	return best
}

// outline draws r's edges, thick px wide, inward.
func outline(img *image.RGBA, r image.Rectangle, thick int, c color.RGBA) {
	fill(img, image.Rect(r.Min.X, r.Min.Y, r.Max.X, r.Min.Y+thick), c)
	fill(img, image.Rect(r.Min.X, r.Max.Y-thick, r.Max.X, r.Max.Y), c)
	fill(img, image.Rect(r.Min.X, r.Min.Y, r.Min.X+thick, r.Max.Y), c)
	fill(img, image.Rect(r.Max.X-thick, r.Min.Y, r.Max.X, r.Max.Y), c)
}

func fill(img *image.RGBA, r image.Rectangle, c color.RGBA) {
	draw.Draw(img, r.Intersect(img.Bounds()), image.NewUniform(c), image.Point{}, draw.Src)
}

// labelSize is how big label draws lines.
func labelSize(lines []string, scale int) image.Point {
	width := 0
	for _, line := range lines {
		width = max(width, len([]rune(line)))
	}
	return image.Pt(width*4*scale+scale, len(lines)*6*scale+scale)
}

// label writes lines in upper case on a dark tag: the first (a pick's
// number and name) in the outlines' color, the rest (its note) white.
func label(img *image.RGBA, tag image.Rectangle, lines []string, scale int) {
	cell, pad, row := 4*scale, scale, 6*scale
	draw.Draw(img, tag, image.NewUniform(color.RGBA{0x10, 0x10, 0x14, 0xE0}), image.Point{}, draw.Over)
	for n, line := range lines {
		ink := momentInk
		if n > 0 {
			ink = color.RGBA{0xFF, 0xFF, 0xFF, 0xFF}
		}
		for i, ch := range []rune(strings.ToUpper(line)) {
			bits := glyphs[ch]
			for y := 0; y < 5; y++ {
				for x := 0; x < 3; x++ {
					if len(bits) == 15 && bits[y*3+x] == '1' {
						px, py := tag.Min.X+pad+i*cell+x*scale, tag.Min.Y+pad+n*row+y*scale
						fill(img, image.Rect(px, py, px+scale, py+scale), ink)
					}
				}
			}
		}
	}
}

// noteLines is a pick's label: its number and name (at most 18 letters),
// then its note in lines of at most 24, two of them (the rest cut).
func noteLines(n int, pick plays.Pick) []string {
	lines := []string{shortened(fmt.Sprintf("%d %s", n, pick.Name()), 18)}
	line := ""
	for _, word := range strings.Fields(pick.Note) {
		for len([]rune(word)) > 24 { // a word longer than a line, broken
			lines, line = append(lines, strings.TrimSpace(line)), ""
			lines, word = append(lines, string([]rune(word)[:24])), string([]rune(word)[24:])
		}
		if len([]rune(line+word)) > 24 {
			lines, line = append(lines, strings.TrimSpace(line)), ""
		}
		line += word + " "
	}
	lines = slices.DeleteFunc(append(lines, strings.TrimSpace(line)), func(l string) bool { return l == "" })
	if len(lines) > 3 { // the rest cut
		last := []rune(lines[2])
		lines = append(lines[:2], strings.TrimSpace(string(last[:min(len(last), 23)]))+"…")
	}
	return lines
}

// gameArea is the picture without its letterbox bars: runs of the bars'
// one color as wide on both sides (or above and below), the game between.
func gameArea(img *image.RGBA) image.Rectangle {
	b := img.Bounds()
	uniform := func(r image.Rectangle, c color.RGBA) bool {
		for y := r.Min.Y; y < r.Max.Y; y++ {
			for x := r.Min.X; x < r.Max.X; x++ {
				if img.RGBAAt(x, y) != c {
					return false
				}
			}
		}
		return true
	}
	bar := func(n int, line func(i int) image.Rectangle) int {
		c, i := img.RGBAAt(line(0).Min.X, line(0).Min.Y), 0
		for i < n/2 && uniform(line(i), c) && uniform(line(n-1-i), c) {
			i++
		}
		return i
	}
	x := bar(b.Dx(), func(i int) image.Rectangle { return image.Rect(i, 0, i+1, b.Dy()) })
	y := bar(b.Dy(), func(i int) image.Rectangle { return image.Rect(0, i, b.Dx(), i+1) })
	if x >= b.Dx()/2 || y >= b.Dy()/2 { // all of it one color: the game's own
		return b
	}
	if x >= y { // bars on one side's axis only: the other's run is the game's (a dark edge)
		y = 0
	} else {
		x = 0
	}
	return image.Rect(x, y, b.Dx()-x, b.Dy()-y)
}

// glyphs is a 3x5 type: rows top first; what isn't here is a space.
var glyphs = map[rune]string{
	'0': "111101101101111", '1': "010110010010111", '2': "111001111100111", '3': "111001111001111", '4': "101101111001001",
	'5': "111100111001111", '6': "111100111101111", '7': "111001001001001", '8': "111101111101111", '9': "111101111001111",
	'A': "010101111101101", 'B': "110101110101110", 'C': "011100100100011", 'D': "110101101101110", 'E': "111100110100111",
	'F': "111100110100100", 'G': "011100101101011", 'H': "101101111101101", 'I': "111010010010111", 'J': "001001001101010",
	'K': "101101110101101", 'L': "100100100100111", 'M': "101111111101101", 'N': "110101101101101", 'O': "010101101101010",
	'P': "110101110100100", 'Q': "010101101110011", 'R': "110101110101101", 'S': "011100010001110", 'T': "111010010010010",
	'U': "101101101101111", 'V': "101101101101010", 'W': "101101111111101", 'X': "101101010101101", 'Y': "101101010010010",
	'Z': "111001010100111", '_': "000000000000111", '-': "000000111000000", '.': "000000000000010", '/': "001001010100100",
	'!': "010010010000010", '?': "111001011000010", ',': "000000000010100", '\'': "010010000000000", ':': "000010000010000",
	'…': "000000000000101", '"': "101101000000000", '<': "001010100010001", '>': "100010001010100",
	'=': "000111000111000", '+': "000010111010000",
}

// pickNames are a moment's picks for a listing: `Pip: "this one clips"`, the note cut short.
func pickNames(m plays.Marker) []string {
	names := make([]string, len(m.Picks))
	for i, pick := range m.Picks {
		names[i] = pick.Name()
		if pick.Note != "" {
			names[i] += fmt.Sprintf(": %q", shortened(pick.Note, 40))
		}
	}
	return names
}

// shortened is text cut to at most n runes, an ellipsis ending what was cut.
func shortened(text string, n int) string {
	if r := []rune(text); len(r) > n {
		return strings.TrimSpace(string(r[:n-1])) + "…"
	}
	return text
}

// momentText is a moment for the model: its picks, numbered as the picture
// labels them, each with where it came from (the file to change).
func momentText(m plays.Marker) string {
	var out strings.Builder
	for i, pick := range m.Picks {
		fmt.Fprintf(&out, "    picked %d: %s\n", i+1, pick.Text())
	}
	return out.String()
}
