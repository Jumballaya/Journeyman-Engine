package art

import (
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
)

func write(t *testing.T, root, rel, text string) {
	t.Helper()
	path := filepath.Join(root, filepath.FromSlash(rel))
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestLoadPalette(t *testing.T) {
	dir := t.TempDir()
	for name, text := range map[string]string{
		"a.gpl": "GIMP Palette\nName: Cormyr\nColumns: 6\n#\n 23  32  56\tblue-1 #172038\n255 255 255\tlight\n",
		"a.hex": "172038\n#FFFFFF\n",
		"a.pal": "JASC-PAL\n0100\n2\n23 32 56\n255 255 255\n",
	} {
		write(t, dir, name, text)
		got, err := LoadPalette(filepath.Join(dir, name))
		if err != nil || len(got) != 2 || got[0].Hex != "#172038" || got[1].Hex != "#ffffff" {
			t.Errorf("%s: %+v %v", name, got, err)
		}
		if name == "a.gpl" && (got[0].Name != "blue-1" || got[1].Name != "light") {
			t.Errorf("gpl names: %+v", got)
		}
	}
	write(t, dir, "bad.gpl", "GIMP Palette\n1 2 x\n")
	if _, err := LoadPalette(filepath.Join(dir, "bad.gpl")); err == nil {
		t.Error("a bad line reads")
	}
}

// A new game's art note reads: no locks yet, so nothing to hold its art to.
func TestScaffoldedNoteReads(t *testing.T) {
	root := t.TempDir()
	if _, err := memory.Scaffold(root); err != nil {
		t.Fatal(err)
	}
	b, ok, err := Load(root)
	if !ok || err != nil || b.Path != Note || b.Palette != "" || len(b.Sizes) != 0 || len(b.Sets) != 0 {
		t.Fatalf("%+v %v %v", b, ok, err)
	}
	write(t, root, "art/kage.png", "")
	if r := Check(root); len(r.Findings) != 0 || len(r.Assets) != 0 {
		t.Errorf("%+v", r)
	}
}

func TestSetTags(t *testing.T) {
	s := Set{Directions: []string{"down", "left"}, Undirected: []string{"down"}, Actions: map[string]Timing{"walk": {150, 150}, "down": {600}}}
	tags := s.Tags()
	if len(tags) != 3 || len(tags["walk_left"]) != 2 || tags["down"] == nil || tags["down_left"] != nil {
		t.Errorf("tags: %v", tags)
	}
	var b Bible
	b.Sizes = map[string]string{"portrait": "48×48", "icon": "16"}
	if w, h := b.Size("missing", "portrait"); w != 48 || h != 48 {
		t.Errorf("portrait %dx%d", w, h)
	}
	if w, h := b.Size("icon"); w != 16 || h != 16 {
		t.Errorf("icon %dx%d", w, h)
	}
}

// A dossier's sprite is planned, drawn, exported or done; its sheet keeps
// the palette, its size and its set's tags and timings.
func TestCheck(t *testing.T) {
	root := t.TempDir()
	write(t, root, "design/Art.md", "---\ntype: art\npalette: art/p.hex\nsizes:\n  character: 2x1\nanimations:\n  character:\n"+
		"    directions: [down, up]\n    idle: [100, 100]\n---\n# Art\n")
	write(t, root, "art/p.hex", "000000\nffffff\n")
	write(t, root, "design/Characters/Kage.md", "---\ntype: character\nsprite: art/kage.aseprite\nart: done\n---\n# Kage\n")
	write(t, root, "design/Characters/Mira.md", "---\ntype: character\n---\n# Mira\n")
	write(t, root, "design/Lore/Fall.md", "---\ntype: lore\n---\n# Fall\n")
	write(t, root, "art/kage.aseprite", "")
	write(t, root, "art/kage.json", `{"frames": [{"frame": {"x": 0, "y": 0, "w": 2, "h": 1}, "sourceSize": {"w": 2, "h": 1}, "duration": 100},
		{"frame": {"x": 2, "y": 0, "w": 2, "h": 1}, "sourceSize": {"w": 2, "h": 1}, "duration": 120}],
		"meta": {"image": "kage.png", "frameTags": [{"name": "idle_down", "from": 0, "to": 1}]}}`)
	img := image.NewNRGBA(image.Rect(0, 0, 4, 1))
	img.Set(0, 0, color.Black)
	img.Set(1, 0, color.NRGBA{200, 0, 0, 255}) // off the palette
	f, _ := os.Create(filepath.Join(root, "art", "kage.png"))
	png.Encode(f, img)
	f.Close()

	r := Check(root)
	if r.Bible == nil || len(r.Palette) != 2 || len(r.Assets) != 2 {
		t.Fatalf("%+v", r)
	}
	kage, mira := r.Assets[0], r.Assets[1]
	if mira.Title != "Mira" || mira.Status != "planned" || mira.Problems != 0 {
		t.Errorf("Mira: %+v", mira)
	}
	if kage.Status != "exported" || kage.Sheet != "art/kage.json" {
		t.Errorf("Kage: %+v", kage)
	}
	var said []string
	for _, f := range r.Findings {
		said = append(said, f.File+": "+f.Message)
	}
	for _, want := range []string{"art/kage.json: no atlas lists it", "art/kage.png: 1 pixels off the palette, the first #c80000 at 1,0",
		"art/kage.json: idle_down's frame 2 is 120 ms", "art/kage.json: the character set's tags it lacks: idle_up"} {
		if !slices.ContainsFunc(said, func(s string) bool { return strings.HasPrefix(s, want) }) {
			t.Errorf("no %q in %q", want, said)
		}
	}
	if kage.Problems != len(r.Findings) {
		t.Errorf("Kage's problems: %d of %d", kage.Problems, len(r.Findings))
	}

	// Listed in an atlas, on the palette, every tag drawn: done, as it says.
	write(t, root, "art/game.atlas.json", `{"sources": ["art/kage.json"]}`)
	img.Set(1, 0, color.White)
	f, _ = os.Create(filepath.Join(root, "art", "kage.png"))
	png.Encode(f, img)
	f.Close()
	write(t, root, "art/kage.json", `{"frames": [{"frame": {"x": 0, "y": 0, "w": 2, "h": 1}, "sourceSize": {"w": 2, "h": 1}, "duration": 100},
		{"frame": {"x": 2, "y": 0, "w": 2, "h": 1}, "sourceSize": {"w": 2, "h": 1}, "duration": 100}],
		"meta": {"image": "kage.png", "frameTags": [{"name": "idle_down", "from": 0, "to": 1}, {"name": "idle_up", "from": 0, "to": 1}]}}`)
	if r := Check(root); len(r.Findings) != 0 || r.Assets[0].Status != "done" {
		t.Errorf("%+v", r)
	}
}

func savePNG(t *testing.T, path string, img image.Image) {
	t.Helper()
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	png.Encode(f, img)
	f.Close()
}

// What breaks the locks shows wherever it's checked from: a sprite an atlas
// lists isn't done with colours off the palette, nor with its picture gone;
// a broken art note is an error, not no note; a palette leading out of the
// game isn't read; tiles keep the grid.
func TestCheckHoldsToTheLocks(t *testing.T) {
	root := t.TempDir()
	write(t, root, "design/Art.md", "---\ntype: art\npalette: art/p.hex\ngrid: 2\n---\n# Art\n")
	write(t, root, "art/p.hex", "000000\nffffff\n")
	write(t, root, "design/Characters/Kage.md", "---\ntype: character\nsprite: art/kage.aseprite\nart: done\n---\n# Kage\n")
	write(t, root, "art/kage.aseprite", "")
	write(t, root, "art/kage.json", `{"frames": [{"frame": {"x": 0, "y": 0, "w": 1, "h": 1}, "sourceSize": {"w": 1, "h": 1}, "duration": 100}], "meta": {"image": "kage.png"}}`)
	write(t, root, "art/game.atlas.json", `{"sources": ["art/kage.json"]}`)
	img := image.NewNRGBA(image.Rect(0, 0, 4, 2))
	img.Set(0, 0, color.NRGBA{200, 0, 0, 255})
	savePNG(t, filepath.Join(root, "art", "kage.png"), img)
	if r := Check(root); r.Assets[0].Status != "exported" || r.Assets[0].Problems != 1 || len(r.Findings) != 1 {
		t.Errorf("off the palette, listed: %+v", r)
	}
	os.Remove(filepath.Join(root, "art", "kage.png"))
	if r := Check(root); r.Assets[0].Status == "done" || len(r.Findings) != 1 || !strings.Contains(r.Findings[0].Message, "isn't there") {
		t.Errorf("its picture gone: %+v", r)
	}

	write(t, root, "maps/t.tsj", `{"tilewidth": 3, "tileheight": 2, "image": "../art/kage.png"}`)
	if r := Check(root); len(r.Findings) != 2 || !strings.Contains(r.Findings[1].Message, "off the 2 px grid") {
		t.Errorf("a tileset off the grid: %+v", r.Findings)
	}

	outside := filepath.Join(t.TempDir(), "p.hex")
	os.WriteFile(outside, []byte("000000\n"), 0o644)
	os.Remove(filepath.Join(root, "art", "p.hex"))
	if os.Symlink(outside, filepath.Join(root, "art", "p.hex")) == nil {
		if r := Check(root); len(r.Palette) != 0 {
			t.Errorf("a palette out of the game was read: %+v", r.Palette)
		}
	}

	write(t, root, "design/Art.md", "---\ntype: art\npalette: [unclosed\n---\n# Art\n")
	if r := Check(root); r.Bible == nil || len(r.Findings) == 0 || r.Findings[0].Level != "error" {
		t.Errorf("a broken art note: %+v", r)
	}
}
