package atlas

import (
	"fmt"
	"image"
	"image/draw"
	"math"
	"sort"
)

// SourceImage is one decoded source PNG along with its atlas region name.
// The caller (cli/cmd/jm/build.go's loadAtlasSources) derives Name from the
// source PNG's basename (without extension).
type SourceImage struct {
	Name string
	Img  image.Image
}

// shelf is a horizontal row in the packer. Items are placed left-to-right;
// y is the shelf's top in atlas pixel coords; height is the tallest item's
// height (plus 2*padding when padding > 0). cursorX is the next free X.
type shelf struct {
	y       int
	height  int
	cursorX int
}

// packerState carries running data through the placement loop.
type packerState struct {
	width   int     // current atlas width (power of two; doubles when items don't fit)
	maxSize int     // cap; placement errors when atlas would exceed this in either dim
	padding int     // pixel buffer around each region (default 0)
	shelves []shelf // ordered top-to-bottom
}

// Pack runs shelf-pack over images, returning the composed atlas (always a
// power-of-two-dimensioned *image.NRGBA, non-premultiplied) plus the per-name
// region rects in pixel space ([x, y, w, h]) suitable for the engine's
// AtlasManager.
//
// Determinism: input is sorted lexically by Name first (input-order
// independence), then stable-sorted by descending height (placement order).
// Output bytes (PNG-encoded) are byte-identical for byte-identical input
// within a pinned Go toolchain version. See package docs for cross-version
// caveats.
//
// Error cases:
//   - len(images) == 0 → error (empty atlas is almost always a config bug)
//   - duplicate Name → error (defensive backstop; the build.go layer detects
//     this earlier with a friendlier source-path-aware message)
//   - any source dimension + 2*padding > maxSize → error naming the source
//   - composed atlas would exceed maxSize in either dim → error naming the dim
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

	// Defensive duplicate-name check. build.go's checkUniqueBasenames catches
	// this earlier with both source paths in the message; here we have only
	// the Name, but we still error rather than silently overwrite.
	seen := map[string]bool{}
	for _, src := range images {
		if seen[src.Name] {
			return nil, nil, fmt.Errorf("atlas: pack: duplicate region name %q", src.Name)
		}
		seen[src.Name] = true
	}

	// Determinism anchor 1: lexical sort by name. Input-order independent.
	sorted := make([]SourceImage, len(images))
	copy(sorted, images)
	sort.SliceStable(sorted, func(i, j int) bool {
		return sorted[i].Name < sorted[j].Name
	})

	// Determinism anchor 2: stable sort by descending height. Ties (same
	// height) preserve the lexical-name order from anchor 1.
	sort.SliceStable(sorted, func(i, j int) bool {
		return sorted[i].Img.Bounds().Dy() > sorted[j].Img.Bounds().Dy()
	})

	// Pre-flight: verify every source fits within maxSize after padding.
	for _, src := range sorted {
		b := src.Img.Bounds()
		if b.Dx()+2*padding > maxSize || b.Dy()+2*padding > maxSize {
			return nil, nil, fmt.Errorf(
				"atlas: pack: source %q (%dx%d + padding %d) exceeds maxSize %d",
				src.Name, b.Dx(), b.Dy(), padding, maxSize)
		}
	}

	// Initial atlas width = next power of two ≥ widest source's slot width.
	widestSlot := 0
	totalArea := 0
	for _, src := range sorted {
		w := src.Img.Bounds().Dx() + 2*padding
		h := src.Img.Bounds().Dy() + 2*padding
		if w > widestSlot {
			widestSlot = w
		}
		totalArea += w * h
	}
	// Initial atlas width: max(widestSlot, ceil(sqrt(totalArea))) rounded to
	// next power of two. The sqrt(area) lower bound prevents pathological tall
	// 32×N atlases when N same-size sources can fit shoulder-to-shoulder. With
	// two 32×32 sources this yields width=64 (atlas 64×32) instead of width=32
	// (atlas 32×64). The doubling loop below still kicks in if we underestimated.
	initialSlot := widestSlot
	areaSqrt := int(math.Ceil(math.Sqrt(float64(totalArea))))
	if areaSqrt > initialSlot {
		initialSlot = areaSqrt
	}
	state := packerState{
		width:   nextPow2(initialSlot),
		maxSize: maxSize,
		padding: padding,
	}

	regions := make(map[string][4]int, len(sorted))

	// Placement loop. For each item, try to place on an existing shelf;
	// if no shelf fits, open a new one. If the new shelf would exceed
	// maxSize in height, double the atlas width and restart from scratch.
	// Restart is at-most O(log(maxSize)) times because doubling rapidly
	// caps out — for sub-2K atlases this is ≤ 5 iterations total.
	for {
		state.shelves = state.shelves[:0]
		regions = make(map[string][4]int, len(sorted))
		failed := false
		nextY := 0

		for _, src := range sorted {
			b := src.Img.Bounds()
			slotW := b.Dx() + 2*padding
			slotH := b.Dy() + 2*padding

			placed := false
			for i := range state.shelves {
				sh := &state.shelves[i]
				if sh.cursorX+slotW <= state.width && slotH <= sh.height {
					regions[src.Name] = [4]int{
						sh.cursorX + padding,
						sh.y + padding,
						b.Dx(),
						b.Dy(),
					}
					sh.cursorX += slotW
					placed = true
					break
				}
			}
			if placed {
				continue
			}

			// Open a new shelf.
			if slotW > state.width {
				// Source is wider than current atlas. Double width and retry
				// the whole placement run.
				if state.width*2 > state.maxSize {
					return nil, nil, fmt.Errorf(
						"atlas: pack: width would exceed maxSize %d (placing %q)",
						state.maxSize, src.Name)
				}
				state.width *= 2
				failed = true
				break
			}
			newShelf := shelf{
				y:       nextY,
				height:  slotH,
				cursorX: slotW,
			}
			regions[src.Name] = [4]int{
				padding,
				nextY + padding,
				b.Dx(),
				b.Dy(),
			}
			state.shelves = append(state.shelves, newShelf)
			nextY += slotH

			if nextY > state.maxSize {
				// Height blew the cap — try doubling width and retry.
				if state.width*2 > state.maxSize {
					return nil, nil, fmt.Errorf(
						"atlas: pack: height would exceed maxSize %d (last placed %q)",
						state.maxSize, src.Name)
				}
				state.width *= 2
				failed = true
				break
			}
		}
		if !failed {
			break
		}
	}

	// Final dimensions: width is already a power of two; height = sum of
	// shelf heights, rounded up to a power of two for GPU friendliness.
	usedHeight := 0
	for _, sh := range state.shelves {
		usedHeight += sh.height
	}
	finalH := nextPow2(usedHeight)
	if finalH > maxSize {
		return nil, nil, fmt.Errorf(
			"atlas: pack: rounded height %d exceeds maxSize %d", finalH, maxSize)
	}

	// Compose: NRGBA destination (canonical for the PNG encoder's
	// deterministic path). draw.Draw handles cross-color-model source images
	// via per-pixel ColorModel().Convert dispatch — Paletted / Gray /
	// NRGBA64 sources all blit correctly without explicit conversion.
	atlas := image.NewNRGBA(image.Rect(0, 0, state.width, finalH))
	for _, src := range sorted {
		rect := regions[src.Name]
		dst := image.Rect(rect[0], rect[1], rect[0]+rect[2], rect[1]+rect[3])
		draw.Draw(atlas, dst, src.Img, src.Img.Bounds().Min, draw.Src)
	}

	return atlas, regions, nil
}

// nextPow2 returns the smallest power of two ≥ n. nextPow2(0) == 1,
// nextPow2(1) == 1, nextPow2(33) == 64.
func nextPow2(n int) int {
	if n <= 1 {
		return 1
	}
	p := 1
	for p < n {
		p <<= 1
	}
	return p
}
