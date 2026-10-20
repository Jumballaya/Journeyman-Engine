package memory

import (
	"os"
	"path/filepath"
	"strings"
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
	// Locked: palette and grid set. Its type dropped, it's still the art note (the art check holds the art to it).
	art, _ := os.ReadFile(filepath.Join(root, Dir, "Art.md"))
	os.WriteFile(filepath.Join(root, Dir, "Art.md"), []byte(strings.NewReplacer("palette:\n", "palette: assets/palettes/war.gpl\n",
		"grid:\n", "grid: 16\n", "type: art\n", "").Replace(string(art))), 0o644)
	Observe(root, "Levels", "level", "The border crossing.")
	if next, _ := NextStep(Onboarding(root, you)); next.Name != "you" {
		t.Errorf("last: %+v", next)
	}
	you.Remember("preference", "Small changes.", "")
	if next, ok := NextStep(Onboarding(root, you)); ok {
		t.Errorf("planned, but next: %+v", next)
	}
}

// The feel noted in Pitch isn't its pitch; a skip of a step whose name is
// already another fact's text is still kept.
func TestOnboardingTellsFeelFromPitchAndKeepsSkips(t *testing.T) {
	root := t.TempDir()
	Scaffold(root)
	you := PersonFacts(filepath.Join(t.TempDir(), "memory.md"))
	Observe(root, "Pitch", "feel", "tense and quiet")
	steps := Onboarding(root, you)
	if next, _ := NextStep(steps); steps[1].Done != true || next.Name != "pitch" {
		t.Errorf("only a feel: %+v", steps[:2])
	}
	topic, _, _ := GameFacts(root).Remember("topic", "world", "")
	if _, err := Skip(root, "world"); err != nil {
		t.Fatal(err)
	}
	Skip(root, "world")
	if !Onboarding(root, you)[4].Done || len(GameFacts(root).All()) != 2 || GameFacts(root).All()[0] != topic {
		t.Errorf("skip world beside a fact \"world\": %+v", GameFacts(root).All())
	}
}

// An answer already remembered under no category of its own takes the step's.
func TestARememberedAnswerTakesItsStepsCategory(t *testing.T) {
	root := t.TempDir()
	plain, _, _ := GameFacts(root).Remember("", "Use 16x16 sprites.", "")
	art, _, err := GameFacts(root).Remember("scope", "Use 16x16 sprites.", "")
	if err != nil || art.ID != plain.ID || art.Category != "scope" || len(GameFacts(root).All()) != 1 || !Onboarding(root, Facts{})[3].Done {
		t.Errorf("%+v %v %+v", art, err, GameFacts(root).All())
	}
	if again, _, _ := GameFacts(root).Remember("", "Use 16x16 sprites.", ""); again.Category != "scope" {
		t.Errorf("a plain remember undid its category: %+v", again)
	}
	if other, _, _ := GameFacts(root).Remember("art", "Use 16x16 sprites.", ""); other.Category != "scope" || other.ID != plain.ID || !Onboarding(root, Facts{})[3].Done {
		t.Errorf("another category took it: %+v", other)
	}
}
