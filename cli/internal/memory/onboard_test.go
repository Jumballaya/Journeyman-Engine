package memory

import (
	"path/filepath"
	"testing"
)

// A new game's plan: each step done as its answer is kept, the optional
// ones skipped, and the person asked about themselves once.
func TestOnboardingFollowsTheNotes(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	you := PersonFacts(filepath.Join(t.TempDir(), "memory.md"))
	if next, ok := NextStep(Onboarding(root, you)); !ok || next.Name != "pitch" {
		t.Fatalf("first: %+v", next)
	}
	Observe(root, "Pitch", "pitch", "A courier crosses a war.")
	Observe(root, "Pitch", "feel", "tense, quiet")
	Observe(root, "Pillars", "pillar", "every route is a choice")
	GameFacts(root).Remember("scope", "No combat in the first version.", "")
	if next, _ := NextStep(Onboarding(root, you)); next.Name != "world" || !next.Optional {
		t.Errorf("after scope: %+v", next)
	}
	NewDossier(root, "location", "Irongate", "", nil)
	if _, err := Skip(root, "characters"); err != nil {
		t.Fatal(err)
	}
	if _, err := Skip(root, "pitch"); err == nil {
		t.Error("skipped the pitch")
	}
	GameFacts(root).Remember("art", "16x16 sprites in assets/art/*.aseprite.", "")
	Observe(root, "Levels", "level", "The border crossing.")
	if next, _ := NextStep(Onboarding(root, you)); next.Name != "you" {
		t.Errorf("last: %+v", next)
	}
	you.Remember("preference", "Small changes.", "")
	if next, ok := NextStep(Onboarding(root, you)); ok {
		t.Errorf("planned, but next: %+v", next)
	}
}
