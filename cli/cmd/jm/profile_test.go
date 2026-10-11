package main

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestFormatProfileShowsTheCostliestAndTheCounts(t *testing.T) {
	var r profileReport
	err := json.Unmarshal([]byte(`{"frames": 120, "frame": {"avgMs": 1.5, "worstMs": 4},
		"systems": [{"name": "ScriptSystem", "avgMs": 0.8, "worstMs": 2}, {"name": "CollisionSystem", "avgMs": 0.1, "worstMs": 0.3}],
		"scripts": [{"name": "assets/scripts/pip.ts", "avgMs": 0.5, "worstMs": 1.9}], "modules": [],
		"counts": {"entities": {"avg": 41.5, "max": 44}, "draw calls": {"avg": 7, "max": 9}}}`), &r)
	if err != nil {
		t.Fatal(err)
	}
	out := formatProfile(r, 1)
	for _, want := range []string{"last 120 frames", "ScriptSystem", "... 1 more", "assets/scripts/pip.ts", "draw calls", "entities"} {
		if !strings.Contains(out, want) {
			t.Errorf("missing %q in\n%s", want, out)
		}
	}
	if strings.Contains(out, "CollisionSystem") || strings.Contains(out, "modules") {
		t.Errorf("more than asked for:\n%s", out)
	}
	if strings.Index(out, "draw calls") > strings.Index(out, "entities") {
		t.Errorf("counts not sorted:\n%s", out)
	}
}
