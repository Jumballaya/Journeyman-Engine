package plays

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
)

// Press is a key or mouse button the person held during a play, in seconds
// from its start. Open: still down when the play ended (To is the end).
type Press struct {
	Input string  `json:"input"` // a key's name ("Space", "MouseLeft"), as bindings name it
	From  float64 `json:"from"`
	To    float64 `json:"to"`
	Open  bool    `json:"open,omitempty"`

	fromFrame, toFrame uint64
}

var mouseButtons = []string{"MouseLeft", "MouseRight", "MouseMiddle"}

// LeadIn is how far before a marker a summary shows what was pressed (seconds):
// enough for the jump or dodge the person marked.
const LeadIn = 2.0

// FrameRunning is the frame running at t seconds: 0 before the start, past the
// last kept frame after it. (Summed float32 dts drift: "1s" starts at 1.00000005.)
func (p *Play) FrameRunning(t float64) uint64 {
	times, _ := p.Times()
	return uint64(sort.Search(max(len(times)-1, 0), func(i int) bool { return times[i+1] > t+1e-4 }))
}

// Presses is every press that overlaps frames [from, to], in the order they
// started: what the person was pressing then (inputs.jsonl, read as holds).
func (p *Play) Presses(from, to uint64) ([]Press, error) {
	f, err := os.Open(filepath.Join(p.Dir, "inputs.jsonl"))
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var all []Press
	down := map[string]int{} // a held input's press, in all
	recorded := p.Recorded()
	scan := bufio.NewScanner(f)
	scan.Buffer(make([]byte, 1<<16), 1<<20)
	for scan.Scan() {
		var e struct {
			F      uint64 `json:"f"`
			Type   string `json:"type"`
			Name   string `json:"name"`
			Button int    `json:"button"`
			Down   bool   `json:"down"`
		}
		if json.Unmarshal(scan.Bytes(), &e) != nil || (e.Type != "key" && e.Type != "button") {
			continue
		}
		input := e.Name
		if e.Type == "button" { // the same control as its key name (the driver's `down MouseLeft`)
			input = fmt.Sprintf("Mouse%d", e.Button)
			if e.Button >= 0 && e.Button < len(mouseButtons) {
				input = mouseButtons[e.Button]
			}
		}
		f := min(e.F, recorded) // a crash can keep inputs past the last frame it kept
		i, held := down[input]
		if e.Down && !held {
			down[input] = len(all)
			all = append(all, Press{Input: input, From: ms(p.TimeOf(f)), Open: true, fromFrame: f, toFrame: ^uint64(0)})
		} else if !e.Down && held {
			// Down and up around one frame (the driver's press): it was held for that frame.
			all[i].To, all[i].Open, all[i].toFrame = ms(p.TimeOf(min(recorded, max(f, all[i].fromFrame+1)))), false, f
			delete(down, input)
		}
	}
	end := ms(p.TimeOf(p.Recorded()))
	out := []Press{}
	for _, press := range all {
		if press.fromFrame > to || press.toFrame < from {
			continue
		}
		if press.Open {
			press.To = end
		}
		out = append(out, press)
	}
	return out, scan.Err()
}
