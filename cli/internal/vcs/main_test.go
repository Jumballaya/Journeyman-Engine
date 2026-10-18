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
	// No auto maintenance: git (2.47+) runs it detached after a commit, still
	// writing in .git when a test's folder is removed.
	os.Setenv("GIT_CONFIG_COUNT", "1")
	os.Setenv("GIT_CONFIG_KEY_0", "maintenance.auto")
	os.Setenv("GIT_CONFIG_VALUE_0", "false")
	code := m.Run()
	os.RemoveAll(home)
	os.Exit(code)
}
