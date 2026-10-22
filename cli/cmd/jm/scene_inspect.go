package main

import (
	"encoding/json"
	"fmt"
	"image"
	"math"
	"os"
	"path/filepath"
	"strings"
)

// A scene (or a prefab) as the engine builds it: JM_INSPECT runs the game's
// build headless, draws the scene once without stepping it, and writes what
// it made (engine/core/app/agent/Inspect.cpp). Tools show that, not their
// own reading of scene files: prefabs, overrides, children, cameras are the
// engine's.

// inspection is JM_INSPECT's file.
type inspection struct {
	Entities []inspected `json:"entities"`
	Draw     struct {
		World []struct {
			Entity  json.RawMessage `json:"entity"`
			Image   string          `json:"image"`
			TexRect []float64       `json:"texRect"` // u, v, w, h of the image, 0..1; none: all of it
		} `json:"world"`
	} `json:"draw"`
	Camera *struct{ X, Y, Width, Height float64 }
	Error  string `json:"error"`
}

// inspected is an entity the engine made: where its entry is (entry: "3",
// "3/c1" its child, "3/p0" its prefab's), the JSON it was made from, where
// it is in the world (its transform's live fields).
type inspected struct {
	ID         json.RawMessage `json:"id"`
	Name       string          `json:"name"`
	Origin     struct{ Entry, Prefab string }
	Components map[string]any `json:"components"`
	World      *struct{ X, Y, Z, ScaleX, ScaleY, Rotation float64 }
}

// inspect is file (a scene or prefab of the game at root) as the engine
// builds it, the game built first if its build is stale (tests without an
// engine stand in for it: fakeInspect).
var inspect = engineInspect

func engineInspect(c *toolCall, root, file string) (inspection, error) {
	if err := freshBuild(c, root); err != nil {
		return inspection{}, err
	}
	engine, err := resolveEnginePath()
	if err == nil {
		engine, err = filepath.Abs(engine) // the run starts in the game
	}
	if err != nil {
		return inspection{}, err
	}
	work, err := os.MkdirTemp("", "jm-inspect-")
	if err != nil {
		return inspection{}, err
	}
	defer os.RemoveAll(work)
	run := engineRun(c.context(), engine, root, filepath.Join(root, "build"))
	run.Env = append(os.Environ(), "JM_HEADLESS=1", "JM_RENDERER=none", "JM_ENTRY_SCENE="+filepath.ToSlash(file),
		"JM_INSPECT="+filepath.Join(work, "seen.json"), "JM_SAVE_DIR="+filepath.Join(work, "save"), "JM_ERRORS="+filepath.Join(work, "errors.jsonl"))
	if out, err := run.CombinedOutput(); err != nil {
		errors, _ := os.ReadFile(filepath.Join(work, "errors.jsonl"))
		return inspection{}, fmt.Errorf("the engine couldn't show %s (%v): %s", file, err, strings.TrimSpace(firstLine(string(errors), string(out))))
	}
	var seen inspection
	data, err := os.ReadFile(filepath.Join(work, "seen.json"))
	if err == nil {
		err = json.Unmarshal(data, &seen)
	}
	return seen, err
}

// placed are the inspected entities where the engine put them, each by where
// its entry is in the file ("entities[1].children[0]"; a prefab's child,
// by its name: "entities[0]/Shadow") and how it looks as it starts (look:
// the image it's drawn with, and the part of it, as lookImage reads).
func (seen inspection) placed() []placedEntity {
	looks := map[string]string{}
	for _, d := range seen.Draw.World {
		if id := string(d.Entity); looks[id] == "" && d.Image != "white" && d.Image != "" {
			looks[id] = d.Image
			if len(d.TexRect) == 4 {
				looks[id] = fmt.Sprintf("%s#%g,%g,%g,%g", d.Image, d.TexRect[0], d.TexRect[1], d.TexRect[2], d.TexRect[3])
			}
		}
	}
	where := map[string]string{} // by entry key: "3", "3/c1", "3/p0"
	out := []placedEntity{}
	for _, e := range seen.Entities {
		key := e.Origin.Entry
		parent, last, nested := cutLast(key, "/")
		p := placedEntity{Name: e.Name, Components: e.Components, Look: looks[string(e.ID)], Inherited: strings.Contains(key, "/p")}
		switch {
		case !nested:
			p.Where = "entities[" + key + "]"
		case strings.HasPrefix(last, "c"):
			p.Where = where[parent] + ".children[" + last[1:] + "]"
		default:
			p.Where = where[parent] + "/" + e.Name
		}
		where[key] = p.Where
		if !p.Inherited {
			p.Prefab = e.Origin.Prefab
		}
		if t := e.World; t != nil {
			p.X, p.Y, p.Z, p.Rotation = t.X, t.Y, t.Z, t.Rotation
			if own, _ := e.Components["TransformComponent"].(map[string]any); own["scale"] != nil { // scale is a half size; none: a point
				p.W, p.H = 2*math.Abs(t.ScaleX), 2*math.Abs(t.ScaleY)
				p.FlipX, p.FlipY = t.ScaleX < 0, t.ScaleY < 0
			}
		}
		out = append(out, p)
	}
	return out
}

func cutLast(s, sep string) (before, after string, found bool) {
	i := strings.LastIndex(s, sep)
	if i < 0 {
		return "", s, false
	}
	return s[:i], s[i+len(sep):], true
}

// screen is what the game shows of the scene as it starts: the camera's view
// (a camera entity's, following its target, kept in its bounds), camera 1
// when an entity is the camera; false when the engine drew none.
func (seen inspection) screen() (map[string]float64, bool) {
	c := seen.Camera
	if c == nil || c.Width <= 0 || c.Height <= 0 {
		return nil, false
	}
	s := map[string]float64{"x": c.X, "y": c.Y, "w": c.Width, "h": c.Height, "camera": 0}
	for _, e := range seen.Entities {
		if _, ok := e.Components["CameraComponent"]; ok && e.World != nil {
			s["camera"] = 1
		}
	}
	return s, true
}

// lookImage is a look's picture: an image of the game's build, or the part
// of it a look names ("assets/fx.atlas.png#0,0,0.5,1": u, v, w, h).
func lookImage(root, look string) image.Image {
	file, part, cut := strings.Cut(look, "#")
	img := readGameImage(filepath.Join(root, "build"), file)
	var u, v, w, h float64
	if img == nil || !cut {
		return img
	}
	if n, _ := fmt.Sscanf(part, "%g,%g,%g,%g", &u, &v, &w, &h); n != 4 {
		return nil
	}
	b := img.Bounds()
	x0, y0 := b.Min.X+int(math.Round(u*float64(b.Dx()))), b.Min.Y+int(math.Round(v*float64(b.Dy())))
	rect := image.Rect(x0, y0, x0+int(math.Round(w*float64(b.Dx()))), y0+int(math.Round(h*float64(b.Dy())))).Intersect(b)
	sub, ok := img.(interface {
		SubImage(image.Rectangle) image.Image
	})
	if !ok || rect.Empty() {
		return nil
	}
	return sub.SubImage(rect)
}
