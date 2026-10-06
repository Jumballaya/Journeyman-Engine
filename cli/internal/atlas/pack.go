package atlas

import (
	"cmp"
	"fmt"
	"image"
	"image/draw"
	"math"
	"slices"
)

// SourceImage is one decoded source PNG and its atlas region name (the PNG's
// basename without extension, chosen by build.go's loadAtlasSources).
type SourceImage struct {
	Name string
	Img  image.Image
}

// Pack shelf-packs images into one power-of-two *image.NRGBA no larger than
// maxSize in either dimension, returning it with each name's [x, y, w, h]
// pixel rect. padding pixels surround every region.
//
// Output is deterministic and independent of input order: sources are placed
// tallest first, ties broken by name. Duplicate names are an error (build.go
// catches them earlier with a friendlier, path-aware message).
func Pack(images []SourceImage, padding, maxSize int) (*image.NRGBA, map[string][4]int, error) {
	if len(images) == 0 {
		return nil, nil, fmt.Errorf("atlas: pack: no sources")
	}
	if padding < 0 {
		return nil, nil, fmt.Errorf("atlas: pack: negative padding %d", padding)
	}
	if maxSize <= 0 {
		return nil, nil, fmt.Errorf("atlas: pack: non-positive maxSize %d", maxSize)
	}

	sorted := slices.Clone(images)
	slices.SortFunc(sorted, func(a, b SourceImage) int {
		return cmp.Or(b.Img.Bounds().Dy()-a.Img.Bounds().Dy(), cmp.Compare(a.Name, b.Name))
	})
	widest, area, seen := 0, 0, map[string]bool{}
	for _, src := range sorted {
		if seen[src.Name] {
			return nil, nil, fmt.Errorf("atlas: pack: duplicate region name %q", src.Name)
		}
		seen[src.Name] = true
		b := src.Img.Bounds()
		w, h := b.Dx()+2*padding, b.Dy()+2*padding
		if w > maxSize || h > maxSize {
			return nil, nil, fmt.Errorf("atlas: pack: source %q (%dx%d + padding %d) exceeds maxSize %d",
				src.Name, b.Dx(), b.Dy(), padding, maxSize)
		}
		widest = max(widest, w)
		area += w * h
	}

	// Starting at sqrt(area) keeps same-size sources side by side rather than
	// in a tall 32xN strip; the width then doubles until the shelves fit.
	width := nextPow2(max(widest, int(math.Ceil(math.Sqrt(float64(area))))))
	for width > maxSize && width/2 >= widest {
		width /= 2
	}
	for {
		if width > maxSize {
			return nil, nil, fmt.Errorf("atlas: pack: sources don't fit in a %dx%d atlas", maxSize, maxSize)
		}
		regions, usedHeight := shelfPack(sorted, padding, width)
		if height := nextPow2(usedHeight); height <= maxSize {
			atlas := image.NewNRGBA(image.Rect(0, 0, width, height))
			for _, src := range sorted {
				r := regions[src.Name]
				draw.Draw(atlas, image.Rect(r[0], r[1], r[0]+r[2], r[1]+r[3]), src.Img, src.Img.Bounds().Min, draw.Src)
			}
			return atlas, regions, nil
		}
		width *= 2
	}
}

// shelfPack places each source on the first shelf (row) with room, opening a
// new shelf below when none has. Every slot must be at most width wide.
func shelfPack(sorted []SourceImage, padding, width int) (map[string][4]int, int) {
	type shelf struct{ y, height, cursorX int }
	var shelves []shelf
	regions := make(map[string][4]int, len(sorted))
	usedHeight := 0
	for _, src := range sorted {
		b := src.Img.Bounds()
		slotW, slotH := b.Dx()+2*padding, b.Dy()+2*padding
		i := slices.IndexFunc(shelves, func(sh shelf) bool {
			return sh.cursorX+slotW <= width && slotH <= sh.height
		})
		if i < 0 {
			shelves = append(shelves, shelf{y: usedHeight, height: slotH})
			usedHeight += slotH
			i = len(shelves) - 1
		}
		sh := &shelves[i]
		regions[src.Name] = [4]int{sh.cursorX + padding, sh.y + padding, b.Dx(), b.Dy()}
		sh.cursorX += slotW
	}
	return regions, usedHeight
}

// nextPow2 returns the smallest power of two >= n (1 for n <= 1).
func nextPow2(n int) int {
	p := 1
	for p < n {
		p <<= 1
	}
	return p
}
