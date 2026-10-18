package main

import (
	"fmt"
	"image"
	"image/color"
	"image/draw"
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
		if pick.Kind == "point" { // the label above the crosshair's arms
			r.Min.Y -= 5 * scale
		}
		label(img, r, fmt.Sprintf("%d %s", i+1, pick.Name()), scale)
	}
	return img
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

// label writes text (upper case, at most 18 letters) on a dark tag above r,
// or inside its top when r is at the picture's top.
func label(img *image.RGBA, r image.Rectangle, text string, scale int) {
	text = strings.ToUpper(text)
	if len(text) > 18 {
		text = text[:17] + "."
	}
	cell, pad := 4*scale, scale
	tag := image.Rect(0, 0, len(text)*cell+pad, 5*scale+2*pad)
	at := image.Pt(r.Min.X, r.Min.Y-tag.Dy())
	if at.Y < 0 {
		at.Y = r.Min.Y
	}
	at.X = min(at.X, img.Bounds().Dx()-tag.Dx())
	tag = tag.Add(image.Pt(max(0, at.X), at.Y))
	draw.Draw(img, tag, image.NewUniform(color.RGBA{0x10, 0x10, 0x14, 0xE0}), image.Point{}, draw.Over)
	for i, ch := range text {
		bits := glyphs[ch]
		for row := 0; row < 5; row++ {
			for col := 0; col < 3; col++ {
				if len(bits) == 15 && bits[row*3+col] == '1' {
					x, y := tag.Min.X+pad+i*cell+col*scale, tag.Min.Y+pad+row*scale
					draw.Draw(img, image.Rect(x, y, x+scale, y+scale), image.NewUniform(momentInk), image.Point{}, draw.Src)
				}
			}
		}
	}
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
}

func pickNames(m plays.Marker) []string {
	names := make([]string, len(m.Picks))
	for i, pick := range m.Picks {
		names[i] = pick.Name()
	}
	return names
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
