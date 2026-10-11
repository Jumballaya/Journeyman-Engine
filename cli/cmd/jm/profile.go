package main

import (
	"encoding/json"
	"fmt"
	"os"
	"sort"
	"strings"
)

// profileReport is the engine's profile (JM_PROFILE, the driver's profile):
// where the last frames' time went, each section's average and worst.
type profileReport struct {
	Frames int `json:"frames"`
	Frame  struct {
		AvgMs   float64 `json:"avgMs"`
		WorstMs float64 `json:"worstMs"`
	} `json:"frame"`
	Systems []profileSection `json:"systems"`
	Scripts []profileSection `json:"scripts"`
	Modules []profileSection `json:"modules"`
	Counts  map[string]struct {
		Avg float64 `json:"avg"`
		Max float64 `json:"max"`
	} `json:"counts"`
}

type profileSection struct {
	Name    string  `json:"name"`
	AvgMs   float64 `json:"avgMs"`
	WorstMs float64 `json:"worstMs"`
}

// readProfile reads the profile an engine wrote at exit.
func readProfile(path string) (profileReport, error) {
	var r profileReport
	data, err := os.ReadFile(path)
	if err != nil {
		return r, fmt.Errorf("the game wrote no profile: %w", err)
	}
	return r, json.Unmarshal(data, &r)
}

// formatProfile is a profile as a table: the frame, then each kind's costliest
// sections (top at most), then the counts.
func formatProfile(r profileReport, top int) string {
	var b strings.Builder
	fmt.Fprintf(&b, "Profile of the last %d frames (ms: average, worst)\n", r.Frames)
	fmt.Fprintf(&b, "  %-44s %7.2f %7.2f\n", "frame", r.Frame.AvgMs, r.Frame.WorstMs)
	for _, kind := range []struct {
		name     string
		sections []profileSection
	}{{"systems", r.Systems}, {"scripts", r.Scripts}, {"modules", r.Modules}} {
		if len(kind.sections) == 0 {
			continue
		}
		fmt.Fprintf(&b, "%s\n", kind.name)
		for i, s := range kind.sections {
			if i == top {
				fmt.Fprintf(&b, "  ... %d more\n", len(kind.sections)-top)
				break
			}
			fmt.Fprintf(&b, "  %-44s %7.2f %7.2f\n", s.Name, s.AvgMs, s.WorstMs)
		}
	}
	names := make([]string, 0, len(r.Counts))
	for name := range r.Counts {
		names = append(names, name)
	}
	sort.Strings(names)
	if len(names) > 0 {
		b.WriteString("counts (average, max)\n")
	}
	for _, name := range names {
		fmt.Fprintf(&b, "  %-44s %7.1f %7.0f\n", name, r.Counts[name].Avg, r.Counts[name].Max)
	}
	return b.String()
}
