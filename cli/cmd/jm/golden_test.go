package main

import (
	"image"
	"image/color"
	"testing"
)

func filled(w, h int, c color.NRGBA) *image.NRGBA {
	img := image.NewNRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.SetNRGBA(x, y, c)
		}
	}
	return img
}

func TestSmallColorShiftsPassAndChangedPixelsCount(t *testing.T) {
	golden := filled(10, 10, color.NRGBA{100, 100, 100, 255})
	actual := filled(10, 10, color.NRGBA{120, 90, 100, 255}) // a different GPU's rounding
	if fraction, _ := compareImages(golden, actual, 40); fraction != 0 {
		t.Fatalf("a 20-step shift should pass at threshold 40, got %v", fraction)
	}
	for x := 0; x < 5; x++ {
		actual.SetNRGBA(x, 0, color.NRGBA{255, 0, 0, 255}) // 5 of 100 pixels really changed
	}
	fraction, diff := compareImages(golden, actual, 40)
	if fraction != 0.05 {
		t.Fatalf("got %v, want 0.05", fraction)
	}
	if diff.At(0, 0) != (color.NRGBA{255, 0, 0, 255}) || diff.At(9, 9) == (color.NRGBA{255, 0, 0, 255}) {
		t.Fatal("the diff image should mark exactly the changed pixels")
	}
}

func TestDifferentSizesDifferEntirely(t *testing.T) {
	if fraction, _ := compareImages(filled(4, 4, color.NRGBA{}), filled(4, 5, color.NRGBA{}), 40); fraction != 1 {
		t.Fatalf("got %v", fraction)
	}
}

// A 2x display's capture averages down to the window's size.
func TestRetinaCapturesScaleToTheWindow(t *testing.T) {
	big := filled(4, 2, color.NRGBA{0, 0, 0, 255})
	big.SetNRGBA(0, 0, color.NRGBA{200, 200, 200, 255})
	big.SetNRGBA(1, 1, color.NRGBA{200, 200, 200, 255})
	small, err := scaleTo(big, 2, 1)
	if err != nil {
		t.Fatal(err)
	}
	if got := small.At(0, 0).(color.NRGBA); got != (color.NRGBA{100, 100, 100, 255}) {
		t.Fatalf("got %v", got)
	}
	if _, err := scaleTo(big, 3, 1); err == nil {
		t.Fatal("a size that isn't a whole multiple should be an error")
	}
}
