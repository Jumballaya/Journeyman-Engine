package vcs

import (
	"os"
	"testing"
)

// Restore takes the games' lock beside the editor's settings: the tests' own, never the person's.
func TestMain(m *testing.M) {
	home, err := os.MkdirTemp("", "vcs-home")
	if err != nil {
		panic(err)
	}
	for _, v := range []string{"HOME", "APPDATA", "XDG_DATA_HOME"} {
		os.Setenv(v, home)
	}
	code := m.Run()
	os.RemoveAll(home)
	os.Exit(code)
}
