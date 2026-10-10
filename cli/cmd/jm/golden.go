package main

import (
	"encoding/json"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/atomicfile"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"

	"github.com/spf13/cobra"
)

// goldenSpec is tests/golden/<name>.golden.json: how to reach the frames, and
// how close they must stay to the recorded images (tests/golden/<name>/).
type goldenSpec struct {
	Replay    string         `json:"replay,omitempty"`    // inputs, relative to tests/golden
	Scene     string         `json:"scene,omitempty"`     // start here instead of the entry scene
	Session   map[string]any `json:"session,omitempty"`   // game state set before the first frame
	Frames    []int          `json:"frames"`              // frame numbers to compare
	Threshold int            `json:"threshold,omitempty"` // a channel differing by more marks the pixel (default 40 of 255)
	MaxDiff   float64        `json:"maxDiff,omitempty"`   // fraction of pixels that may differ (default 0.005)
}

const goldenDir = "tests/golden"

var goldenUpdate bool

var goldenCmd = &cobra.Command{
	Use:   "golden [name...]",
	Short: "Compare the game's frames with recorded golden images",
	Long: `Plays each tests/golden/<name>.golden.json headless from the build and
compares the frames it lists with tests/golden/<name>/frame_NNNNN.png:

  {"replay": "boss.replay.txt", "scene": "scenes/boss.scene.json",
   "session": {"lives": 1}, "frames": [60, 300]}

replay (inputs, as JM_INPUT_REPLAY), scene and session (a deep link: start
there, with that state) are optional. A frame passes when at most maxDiff
(default 0.5%) of its pixels differ by more than threshold (default 40 of
255) in a channel, so different GPUs agree; frames are compared at the
window's size, which headless runs render at on every machine. A failing frame
leaves its capture and a diff image in build/golden/<name>/. The run is
strict: an error the game logs fails it too.

--update records the images instead (after you've looked at them). Run
jm build first. --json reports like jm build --json.`,
	RunE: func(cmd *cobra.Command, args []string) error {
		specs, err := filepath.Glob(filepath.Join(goldenDir, "*.golden.json"))
		if err != nil || len(specs) == 0 {
			return fmt.Errorf("no goldens: add %s/<name>.golden.json (see jm golden --help)", goldenDir)
		}
		names := []string{}
		for _, s := range specs {
			name := strings.TrimSuffix(filepath.Base(s), ".golden.json")
			if len(args) == 0 || slices.Contains(args, name) {
				names = append(names, name)
			}
		}
		if len(names) == 0 {
			return fmt.Errorf("no golden named %s in %s", strings.Join(args, ", "), goldenDir)
		}
		manifestPath := filepath.Join("build", archive.ManifestEntryKey)
		man, err := manifest.LoadManifest(manifestPath)
		if err != nil {
			fail(Diagnostic{Category: "golden", Message: "no build to run (jm build first): " + err.Error()})
		}
		engine, err := resolveEnginePath()
		if err == nil {
			engine, err = filepath.Abs(engine) // the runs start in build/
		}
		if err != nil {
			fail(Diagnostic{Category: "golden", Message: err.Error()})
		}
		width, height := windowSize(man)
		ok := true
		for _, name := range names {
			ok = checkGolden(engine, name, width, height) && ok
		}
		finish(ok)
		return nil
	},
}

func init() {
	goldenCmd.Flags().BoolVar(&goldenUpdate, "update", false, "record the frames as the new golden images")
	goldenCmd.Flags().BoolVar(&jsonOutput, "json", false, "problems as JSON lines, no progress (for tools)")
}

// windowSize is the game window's size from the manifest (1280x720 if unset):
// the size goldens are kept at.
func windowSize(man manifest.GameManifest) (int, int) {
	width, height := 1280, 720
	if win, ok := man.Config["window"].(map[string]any); ok {
		if w, ok := win["width"].(float64); ok {
			width = int(w)
		}
		if h, ok := win["height"].(float64); ok {
			height = int(h)
		}
	}
	return width, height
}

// checkGolden runs one golden and compares (or records) its frames.
func checkGolden(engine, name string, width, height int) bool {
	specPath := filepath.Join(goldenDir, name+".golden.json")
	var spec goldenSpec
	data, err := os.ReadFile(specPath)
	if err == nil {
		err = json.Unmarshal(data, &spec)
	}
	if err == nil && len(spec.Frames) == 0 {
		err = fmt.Errorf("lists no frames")
	}
	if err != nil {
		emit(Diagnostic{Level: "error", Category: "golden", File: specPath, Message: err.Error()})
		return false
	}
	if spec.Threshold <= 0 {
		spec.Threshold = 40
	}
	if spec.MaxDiff <= 0 {
		spec.MaxDiff = 0.005
	}

	work, err := os.MkdirTemp("", "jm-golden-")
	if err != nil {
		emit(Diagnostic{Level: "error", Category: "golden", Message: err.Error()})
		return false
	}
	defer os.RemoveAll(work)
	frames := make([]string, len(spec.Frames))
	for i, f := range spec.Frames {
		frames[i] = fmt.Sprint(f)
	}
	env := append(os.Environ(), "JM_HEADLESS=1", "JM_STRICT=1",
		"JM_EXIT_AFTER_FRAMES="+fmt.Sprint(slices.Max(spec.Frames)+1),
		"JM_SAVE_DIR="+filepath.Join(work, "save"), "JM_CAPTURE_DIR="+filepath.Join(work, "frames"),
		"JM_CAPTURE_FRAMES="+strings.Join(frames, ","), "JM_ERRORS="+filepath.Join(work, "errors.jsonl"))
	if spec.Replay != "" {
		replay, _ := filepath.Abs(filepath.Join(goldenDir, spec.Replay))
		env = append(env, "JM_INPUT_REPLAY="+replay)
	}
	if spec.Scene != "" {
		env = append(env, "JM_ENTRY_SCENE="+spec.Scene)
	}
	if len(spec.Session) > 0 {
		session := filepath.Join(work, "session.json")
		data, _ := json.Marshal(spec.Session)
		if err := os.WriteFile(session, data, 0644); err != nil {
			emit(Diagnostic{Level: "error", Category: "golden", Message: err.Error()})
			return false
		}
		env = append(env, "JM_SESSION="+session)
	}
	run := exec.Command(engine, ".")
	run.Dir = "build"
	run.Env = env
	if out, err := run.CombinedOutput(); err != nil {
		errors, _ := os.ReadFile(filepath.Join(work, "errors.jsonl"))
		emit(Diagnostic{Level: "error", Category: "golden", File: specPath,
			Message: fmt.Sprintf("the run failed (%v): %s", err, strings.TrimSpace(firstLine(string(errors), string(out))))})
		return false
	}

	ok := true
	for _, frame := range spec.Frames {
		file := fmt.Sprintf("frame_%05d.png", frame)
		actual, err := readPNG(filepath.Join(work, "frames", file))
		if err == nil {
			actual, err = scaleTo(actual, width, height)
		}
		if err != nil {
			emit(Diagnostic{Level: "error", Category: "golden", File: specPath, Message: fmt.Sprintf("frame %d: %v", frame, err)})
			ok = false
			continue
		}
		goldenPath := filepath.Join(goldenDir, name, file)
		if goldenUpdate {
			if err := savePNG(goldenPath, actual); err != nil {
				emit(Diagnostic{Level: "error", Category: "golden", File: goldenPath, Message: err.Error()})
				ok = false
			} else {
				say("Recorded %s", goldenPath)
			}
			continue
		}
		golden, err := readPNG(goldenPath)
		if err != nil {
			emit(Diagnostic{Level: "error", Category: "golden", File: goldenPath, Message: "no golden image (record it with jm golden --update)"})
			ok = false
			continue
		}
		fraction, diff := compareImages(golden, actual, spec.Threshold)
		if fraction <= spec.MaxDiff {
			say("%s frame %d: ok (%.2f%% of pixels differ)", name, frame, fraction*100)
			continue
		}
		ok = false
		out := filepath.Join("build", "golden", name)
		_ = savePNG(filepath.Join(out, strings.Replace(file, ".png", ".actual.png", 1)), actual)
		if diff != nil {
			_ = savePNG(filepath.Join(out, strings.Replace(file, ".png", ".diff.png", 1)), diff)
		}
		emit(Diagnostic{Level: "error", Category: "golden", File: goldenPath,
			Message: fmt.Sprintf("frame %d: %.2f%% of pixels differ (%.2f%% allowed); see %s", frame, fraction*100, spec.MaxDiff*100, out)})
	}
	return ok
}

func firstLine(texts ...string) string {
	for _, t := range texts {
		if line, _, _ := strings.Cut(strings.TrimSpace(t), "\n"); line != "" {
			return line
		}
	}
	return ""
}

func readPNG(path string) (image.Image, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return png.Decode(f)
}

func savePNG(path string, img image.Image) error {
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return err
	}
	return atomicfile.Write(path, 0o644, func(w io.Writer) error { return png.Encode(w, img) })
}

// scaleTo averages an image down by a whole factor to width x height (a 2x
// display's capture to the window's size); other sizes are an error.
func scaleTo(img image.Image, width, height int) (image.Image, error) {
	b := img.Bounds()
	if b.Dx() == width && b.Dy() == height {
		return img, nil
	}
	k := b.Dx() / width
	if k < 1 || b.Dx() != width*k || b.Dy() != height*k {
		return nil, fmt.Errorf("captured %dx%d, not a multiple of the window's %dx%d", b.Dx(), b.Dy(), width, height)
	}
	out := image.NewNRGBA(image.Rect(0, 0, width, height))
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			var r, g, bl, a uint32
			for dy := 0; dy < k; dy++ {
				for dx := 0; dx < k; dx++ {
					c := color.NRGBAModel.Convert(img.At(b.Min.X+x*k+dx, b.Min.Y+y*k+dy)).(color.NRGBA)
					r, g, bl, a = r+uint32(c.R), g+uint32(c.G), bl+uint32(c.B), a+uint32(c.A)
				}
			}
			n := uint32(k * k)
			out.SetNRGBA(x, y, color.NRGBA{uint8(r / n), uint8(g / n), uint8(bl / n), uint8(a / n)})
		}
	}
	return out, nil
}

// compareImages is the fraction of pixels where a channel differs by more
// than threshold, and an image of them: the golden in gray, differing pixels
// red. Different sizes differ entirely.
func compareImages(golden, actual image.Image, threshold int) (float64, image.Image) {
	gb, ab := golden.Bounds(), actual.Bounds()
	if gb.Dx() != ab.Dx() || gb.Dy() != ab.Dy() {
		return 1, nil
	}
	diff := image.NewNRGBA(image.Rect(0, 0, gb.Dx(), gb.Dy()))
	differing := 0
	for y := 0; y < gb.Dy(); y++ {
		for x := 0; x < gb.Dx(); x++ {
			g := color.NRGBAModel.Convert(golden.At(gb.Min.X+x, gb.Min.Y+y)).(color.NRGBA)
			a := color.NRGBAModel.Convert(actual.At(ab.Min.X+x, ab.Min.Y+y)).(color.NRGBA)
			if channelDelta(g, a) > threshold {
				differing++
				diff.SetNRGBA(x, y, color.NRGBA{255, 0, 0, 255})
				continue
			}
			gray := uint8((int(g.R) + int(g.G) + int(g.B)) / 3 / 3) // dimmed
			diff.SetNRGBA(x, y, color.NRGBA{gray, gray, gray, 255})
		}
	}
	return float64(differing) / float64(gb.Dx()*gb.Dy()), diff
}

func channelDelta(a, b color.NRGBA) int {
	d := 0
	for _, pair := range [][2]uint8{{a.R, b.R}, {a.G, b.G}, {a.B, b.B}, {a.A, b.A}} {
		v := int(pair[0]) - int(pair[1])
		if v < 0 {
			v = -v
		}
		d = max(d, v)
	}
	return d
}
