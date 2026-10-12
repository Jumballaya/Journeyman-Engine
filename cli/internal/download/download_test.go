package download

import (
	"strings"
	"testing"
)

func TestArchivePathsCantEscape(t *testing.T) {
	if err := WriteFile(t.TempDir(), "../evil", strings.NewReader("x"), 0o644); err == nil {
		t.Fatal("wrote outside the folder")
	}
}
