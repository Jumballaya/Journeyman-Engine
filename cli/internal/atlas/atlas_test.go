package atlas

import (
	"bytes"
	"image"
	"image/color"
	"image/draw"
	"image/png"
	"testing"
)

// makeSourceImage constructs an in-memory NRGBA SourceImage filled with the
// given solid color. No binary fixtures touch the repo.
func makeSourceImage(name string, w, h int, c color.NRGBA) SourceImage {
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	draw.Draw(img, img.Bounds(), &image.Uniform{C: c}, image.Point{}, draw.Src)
	return SourceImage{Name: name, Img: img}
}

// encodePNG round-trips an *image.NRGBA through the stdlib PNG encoder and
// returns the bytes. Used by determinism tests.
func encodePNG(t *testing.T, img *image.NRGBA) []byte {
	t.Helper()
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		t.Fatalf("png.Encode: %v", err)
	}
	return buf.Bytes()
}

func TestPackEmptyConfigReturnsError(t *testing.T) {
	_, _, err := Pack(nil, 0, 4096)
	if err == nil {
		t.Fatal("expected error for empty sources, got nil")
	}
}

func TestPackSingleSourceFitsInPowerOfTwoAtlas(t *testing.T) {
	src := makeSourceImage("only", 32, 32, color.NRGBA{R: 255, A: 255})
	atlas, regions, err := Pack([]SourceImage{src}, 0, 4096)
	if err != nil {
		t.Fatalf("Pack: %v", err)
	}
	if got := atlas.Bounds().Dx(); got != 32 {
		t.Errorf("atlas width: want 32, got %d", got)
	}
	if got := atlas.Bounds().Dy(); got != 32 {
		t.Errorf("atlas height: want 32, got %d", got)
	}
	if got, want := regions["only"], [4]int{0, 0, 32, 32}; got != want {
		t.Errorf("region: want %v, got %v", want, got)
	}
}

func TestPackTwoSourcesShelvedHorizontally(t *testing.T) {
	a := makeSourceImage("a", 32, 32, color.NRGBA{R: 255, A: 255})
	b := makeSourceImage("b", 32, 32, color.NRGBA{G: 255, A: 255})
	atlas, regions, err := Pack([]SourceImage{a, b}, 0, 4096)
	if err != nil {
		t.Fatalf("Pack: %v", err)
	}
	// Two 32x32 sources side by side: width 64 (next pow2), height 32.
	if got, want := atlas.Bounds().Dx(), 64; got != want {
		t.Errorf("width: want %d, got %d", want, got)
	}
	if got, want := atlas.Bounds().Dy(), 32; got != want {
		t.Errorf("height: want %d, got %d", want, got)
	}
	// Both regions must be on y=0, both 32x32, x must differ.
	ra, rb := regions["a"], regions["b"]
	if ra[1] != 0 || rb[1] != 0 {
		t.Errorf("expected y=0 for both, got a=%v b=%v", ra, rb)
	}
	if ra[0] == rb[0] {
		t.Errorf("regions overlap on x: a=%v b=%v", ra, rb)
	}
}

func TestPackVariedSizesShelfPacks(t *testing.T) {
	// Mix of square, tall, and wide sources to exercise non-square handling.
	srcs := []SourceImage{
		makeSourceImage("square_big", 64, 64, color.NRGBA{R: 255, A: 255}),
		makeSourceImage("tall", 16, 64, color.NRGBA{G: 255, A: 255}),
		makeSourceImage("wide", 64, 16, color.NRGBA{B: 255, A: 255}),
		makeSourceImage("small", 16, 16, color.NRGBA{R: 128, G: 128, A: 255}),
	}
	atlas, regions, err := Pack(srcs, 0, 4096)
	if err != nil {
		t.Fatalf("Pack: %v", err)
	}
	if atlas.Bounds().Dx() > 4096 || atlas.Bounds().Dy() > 4096 {
		t.Errorf("atlas exceeds maxSize: %v", atlas.Bounds())
	}
	// No two regions overlap.
	names := []string{"square_big", "tall", "wide", "small"}
	for i := 0; i < len(names); i++ {
		for j := i + 1; j < len(names); j++ {
			ri, rj := regions[names[i]], regions[names[j]]
			ix0, iy0, ix1, iy1 := ri[0], ri[1], ri[0]+ri[2], ri[1]+ri[3]
			jx0, jy0, jx1, jy1 := rj[0], rj[1], rj[0]+rj[2], rj[1]+rj[3]
			overlap := ix0 < jx1 && jx0 < ix1 && iy0 < jy1 && jy0 < iy1
			if overlap {
				t.Errorf("regions %q and %q overlap: %v vs %v",
					names[i], names[j], ri, rj)
			}
		}
	}
}

func TestPackDeterministic(t *testing.T) {
	srcs := []SourceImage{
		makeSourceImage("a", 32, 32, color.NRGBA{R: 255, A: 255}),
		makeSourceImage("b", 16, 48, color.NRGBA{G: 255, A: 255}),
		makeSourceImage("c", 24, 24, color.NRGBA{B: 255, A: 255}),
	}
	atlas1, regions1, err := Pack(srcs, 0, 4096)
	if err != nil {
		t.Fatalf("Pack #1: %v", err)
	}
	atlas2, regions2, err := Pack(srcs, 0, 4096)
	if err != nil {
		t.Fatalf("Pack #2: %v", err)
	}
	if !bytes.Equal(atlas1.Pix, atlas2.Pix) {
		t.Error("atlas pixels differ between runs")
	}
	for k, v := range regions1 {
		if regions2[k] != v {
			t.Errorf("region %q differs: %v vs %v", k, v, regions2[k])
		}
	}
	if !bytes.Equal(encodePNG(t, atlas1), encodePNG(t, atlas2)) {
		t.Error("PNG-encoded bytes differ between runs")
	}
}

func TestPackInvariantUnderInputOrdering(t *testing.T) {
	a := makeSourceImage("a", 32, 32, color.NRGBA{R: 255, A: 255})
	b := makeSourceImage("b", 16, 48, color.NRGBA{G: 255, A: 255})
	c := makeSourceImage("c", 24, 24, color.NRGBA{B: 255, A: 255})

	atlasABC, regionsABC, err := Pack([]SourceImage{a, b, c}, 0, 4096)
	if err != nil {
		t.Fatalf("Pack ABC: %v", err)
	}
	atlasCBA, regionsCBA, err := Pack([]SourceImage{c, b, a}, 0, 4096)
	if err != nil {
		t.Fatalf("Pack CBA: %v", err)
	}
	if !bytes.Equal(atlasABC.Pix, atlasCBA.Pix) {
		t.Error("input ordering changed atlas pixels (sort anchor failed)")
	}
	for k, v := range regionsABC {
		if regionsCBA[k] != v {
			t.Errorf("region %q differs across orderings: %v vs %v",
				k, v, regionsCBA[k])
		}
	}
}

func TestPackErrorsOnSourceTooLarge(t *testing.T) {
	src := makeSourceImage("huge", 256, 256, color.NRGBA{A: 255})
	_, _, err := Pack([]SourceImage{src}, 0, 64)
	if err == nil {
		t.Fatal("expected error for source > maxSize, got nil")
	}
	if !bytes.Contains([]byte(err.Error()), []byte("huge")) {
		t.Errorf("error should name offending source, got: %v", err)
	}
}

func TestPackErrorsOnDuplicateRegionName(t *testing.T) {
	a := makeSourceImage("dup", 16, 16, color.NRGBA{R: 255, A: 255})
	b := makeSourceImage("dup", 16, 16, color.NRGBA{G: 255, A: 255})
	_, _, err := Pack([]SourceImage{a, b}, 0, 4096)
	if err == nil {
		t.Fatal("expected duplicate-name error, got nil")
	}
}
