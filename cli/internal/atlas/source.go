package atlas

import (
	"bytes"
	"errors"
	"fmt"
	"image"
	"image/png"
	"io/fs"
	"path"
	"strconv"
	"strings"
)

// ReadFunc reads a file an atlas names, by its slash path as written there:
// the build reads the working folder, a preview the game's files.
type ReadFunc func(path string) ([]byte, error)

// A Source is one of an atlas's sources as the build reads it: a PNG, one
// region named Name, or an Aseprite sheet's JSON, regions Name_0, Name_1…
type Source struct {
	Path  string // as the atlas names it
	Name  string // its file name without its extension
	Sheet bool
}

// SourceOf is the source the atlas path names; a backslash separates its
// parts, as on Windows (where the atlas was written isn't known).
func SourceOf(p string) Source {
	p = strings.ReplaceAll(p, `\`, "/")
	base := path.Base(p)
	return Source{Path: p, Name: strings.TrimSuffix(base, path.Ext(base)), Sheet: strings.EqualFold(path.Ext(base), ".json")}
}

// Read is config's sources' regions as the build packs them, and each
// sheet's clips by its name; two sources may not make one region name.
func (c AtlasConfig) Read(read ReadFunc, side int) ([]SourceImage, map[string]map[string]Clip, error) {
	var out []SourceImage
	var clips map[string]map[string]Clip
	made := map[string]string{} // region → the source making it
	for _, p := range c.Sources {
		s := SourceOf(p)
		regions, sheetClips, err := s.Read(read, side)
		if err != nil {
			return nil, nil, err
		}
		for _, r := range regions {
			if prior, dup := made[r.Name]; dup {
				return nil, nil, fmt.Errorf("sources %q and %q both produce region name %q", prior, p, r.Name)
			}
			made[r.Name] = p
		}
		if sheetClips != nil {
			if clips == nil {
				clips = map[string]map[string]Clip{}
			}
			clips[s.Name] = sheetClips
		}
		out = append(out, regions...)
	}
	return out, clips, nil
}

// Read is the source's regions as the build cuts them, each with its normal
// map (x.normal.png beside its picture) when there is one, and a sheet's
// clips (nil for a PNG). side bounds a picture's sides (0: any size).
func (s Source) Read(read ReadFunc, side int) ([]SourceImage, map[string]Clip, error) {
	if IsNormalMap(s.Path) {
		return nil, nil, fmt.Errorf("source %q is a normal map: list the image it belongs to", s.Path)
	}
	if !s.Sheet {
		img, normal, err := readPicture(read, s.Path, side, true)
		if err != nil {
			return nil, nil, err
		}
		return []SourceImage{{Name: s.Name, Img: img, Normal: normal}}, nil, nil
	}
	sheet, picture, err := s.sheet(read)
	if err != nil {
		return nil, nil, err
	}
	img, normal, err := readPicture(read, picture, side, true)
	if err != nil {
		return nil, nil, err
	}
	regions, clips, err := sheet.Cut(s.Name, img, normal)
	if err != nil {
		return nil, nil, fmt.Errorf("%q: %w", s.Path, err)
	}
	return regions, clips, nil
}

// Region is region as config's sources make it (a PNG's whole picture, or a
// sheet's frame), no side over side pixels; false when none makes it.
func (c AtlasConfig) Region(read ReadFunc, region string, side int) (image.Image, bool) {
	for _, p := range c.Sources {
		if img := SourceOf(p).region(read, region, side); img != nil {
			return img, true
		}
	}
	return nil, false
}

// region is region as s makes it; nil when it doesn't (hero_1 of a one-frame
// sheet hero.json is another source's) or can't.
func (s Source) region(read ReadFunc, region string, side int) image.Image {
	frame, ok := s.frame(region)
	if !ok {
		return nil
	}
	if !s.Sheet {
		img, _, _ := readPicture(read, s.Path, side, false)
		return img
	}
	sheet, picture, err := s.sheet(read)
	if err != nil {
		return nil
	}
	img, _, err := readPicture(read, picture, side, false)
	if err != nil {
		return nil
	}
	f, _ := sheet.Frame(frame, img, side)
	return f
}

// frame is which of its frames makes region (0 for a PNG's one); false when
// the source doesn't. hero_00 is another source's name, not hero's frame 0.
func (s Source) frame(region string) (int, bool) {
	if !s.Sheet {
		return 0, region == s.Name
	}
	i, err := strconv.Atoi(strings.TrimPrefix(region, s.Name+"_"))
	return i, err == nil && i >= 0 && region == frameName(s.Name, i)
}

// sheet is a sheet source's parsed JSON and its picture's path.
func (s Source) sheet(read ReadFunc) (Sheet, string, error) {
	data, err := read(s.Path)
	if err != nil {
		return Sheet{}, "", err
	}
	sheet, err := ParseSheet(data)
	if err != nil {
		return Sheet{}, "", fmt.Errorf("%q: %w", s.Path, err)
	}
	return sheet, path.Join(path.Dir(s.Path), sheet.Image), nil
}

// readPicture decodes the PNG at p, bounded by side (0: any), and (withNormal)
// its normal map if it has one: nil when it doesn't.
func readPicture(read ReadFunc, p string, side int, withNormal bool) (image.Image, image.Image, error) {
	img, err := decodePNG(read, p, side)
	if err != nil || !withNormal {
		return img, nil, err
	}
	n := NormalPath(p)
	normal, err := decodePNG(read, n, side)
	if errors.Is(err, fs.ErrNotExist) {
		return img, nil, nil
	}
	if err != nil {
		return nil, nil, err
	}
	if normal.Bounds().Size() != img.Bounds().Size() {
		return nil, nil, fmt.Errorf("normal map %q is %v, but %q is %v", n, normal.Bounds().Size(), p, img.Bounds().Size())
	}
	return img, normal, nil
}

func decodePNG(read ReadFunc, p string, side int) (image.Image, error) {
	data, err := read(p)
	if err != nil {
		return nil, fmt.Errorf("read %q: %w", p, err)
	}
	if side > 0 { // its size before its pixels: a huge picture isn't made
		size, err := png.DecodeConfig(bytes.NewReader(data))
		if err != nil {
			return nil, fmt.Errorf("read %q: %w", p, err)
		}
		if size.Width > side || size.Height > side {
			return nil, fmt.Errorf("%q is over %d pixels on a side", p, side)
		}
	}
	img, err := png.Decode(bytes.NewReader(data))
	if err != nil {
		return nil, fmt.Errorf("read %q: %w", p, err)
	}
	return img, nil
}
