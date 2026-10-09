package main

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

func TestOldUnmarkedPlaysArePrunedMarkedOnesKept(t *testing.T) {
	root := t.TempDir()
	write := func(id, markers string) {
		dir := filepath.Join(plays.Root(root), id)
		os.MkdirAll(dir, 0o755)
		os.WriteFile(filepath.Join(dir, "session.json"), []byte(`{"format":1,"frames":1,"markers":`+markers+`}`), 0o644)
	}
	write("2000-01-01_000000", `[{"n":1,"frame":0}]`) // the oldest, but marked
	for i := 1; i <= keptPlays+5; i++ {
		write(fmt.Sprintf("2000-01-02_%06d", i), `[]`)
	}
	pruneOldPlays(root)
	all, _ := plays.List(root)
	if len(all) != keptPlays+1 {
		t.Fatalf("%d plays left, want %d", len(all), keptPlays+1)
	}
	if all[len(all)-1].ID != "2000-01-01_000000" || all[0].ID != fmt.Sprintf("2000-01-02_%06d", keptPlays+5) {
		t.Fatalf("kept the wrong ones: newest %s, oldest %s", all[0].ID, all[len(all)-1].ID)
	}
}
