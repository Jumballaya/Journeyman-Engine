package memory

import (
	"path/filepath"
	"strings"
	"testing"
)

// Remember, the same again, one alike, an update by id, recall and forget,
// in the game and the person's own file.
func TestFactsAreRememberedOnceAndUpdated(t *testing.T) {
	root := t.TempDir()
	game, you := GameFacts(root), PersonFacts(filepath.Join(t.TempDir(), ".jm", "memory.md"))
	exiled, similar, err := game.Remember("canon", "The Dornes were exiled from Varn in 1102.", "")
	if err != nil || len(similar) != 0 || exiled.Scope != "game" || !strings.HasPrefix(exiled.ID, "f") {
		t.Fatalf("remember: %+v %v %v", exiled, similar, err)
	}
	if again, _, _ := game.Remember("canon", "the dornes were exiled from varn in 1102.", ""); again.ID != exiled.ID || len(game.All()) != 1 {
		t.Errorf("the same fact twice: %+v, %d kept", again, len(game.All()))
	}
	_, similar, _ = game.Remember("canon", "The Dornes were exiled from Varn in 1104", "")
	if len(similar) != 1 || similar[0].ID != exiled.ID {
		t.Errorf("alike: %+v", similar)
	}
	updated, _, err := game.Remember("canon", "The Dornes left Varn by choice.", exiled.ID)
	if err != nil || updated.ID != exiled.ID || game.All()[0].Text != "The Dornes left Varn by choice." {
		t.Errorf("update: %+v %v %+v", updated, err, game.All())
	}
	you.Remember("preference", "Keep dialogue under three lines.", "")
	you.Remember("style", "Pixel art at 16x16, warm palette.", "")
	if got := Recall("dialogue lines", game, you); len(got) == 0 || got[0].Scope != "you" || !strings.Contains(got[0].Text, "dialogue") {
		t.Errorf("recall: %+v", got)
	}
	if all := Recall("", game, you); len(all) != 4 {
		t.Errorf("all: %+v", all)
	}
	if n, ok := Find(root, "Memory"); !ok || n.Type != "memory" || len(n.Observations) != 2 {
		t.Errorf("the game's facts are a design note: %+v", n)
	}
	if _, err := game.Forget(exiled.ID); err != nil || len(game.All()) != 1 {
		t.Errorf("forget: %v %+v", err, game.All())
	}
	if _, err := game.Forget("fnope"); err == nil {
		t.Error("forgot a fact that isn't")
	}
	if _, _, err := game.Remember("x", "ends in an id ^abc", ""); err == nil {
		t.Error("a text that ends like a block id")
	}
}
