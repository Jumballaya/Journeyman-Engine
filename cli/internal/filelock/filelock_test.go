package filelock

import (
	"errors"
	"os"
	"path/filepath"
	"sync/atomic"
	"testing"
	"time"
)

// Links to one file hold one lock: the one beside the file they lead to.
func TestLinksToAFileShareItsLock(t *testing.T) {
	dir := t.TempDir()
	real := filepath.Join(dir, "real.json")
	os.WriteFile(real, []byte("{}"), 0o644)
	if os.Symlink("real.json", filepath.Join(dir, "a.json")) != nil || os.Symlink(real, filepath.Join(dir, "b.json")) != nil {
		t.Skip("no symlinks here")
	}
	release, err := Hold(filepath.Join(dir, "a.json"))
	if err != nil {
		t.Fatal(err)
	}
	var got atomic.Int32
	done := make(chan struct{})
	for _, path := range []string{real, filepath.Join(dir, "b.json")} {
		go func() {
			r, err := Hold(path)
			if err == nil {
				got.Add(1)
				r()
			}
			done <- struct{}{}
		}()
	}
	time.Sleep(200 * time.Millisecond) // time to take it, were it free
	if got.Load() != 0 {
		t.Error("another link took the lock while it was held")
	}
	release()
	<-done
	<-done
	if got.Load() != 2 {
		t.Errorf("%d of 2 holders got it after the release", got.Load())
	}
}

// Paths sharing one lock (a link, another spelling on a case-blind volume) are
// refused at once, not waited on, and leave the lock free.
func TestOneFileTwiceIsRefused(t *testing.T) {
	dir := t.TempDir()
	real := filepath.Join(dir, "game.json")
	os.WriteFile(real, []byte("{}"), 0o644)
	aliases := []string{}
	if os.Symlink(real, filepath.Join(dir, "link.json")) == nil {
		aliases = append(aliases, filepath.Join(dir, "link.json"))
	}
	if _, err := os.Stat(filepath.Join(dir, "GAME.JSON")); err == nil {
		aliases = append(aliases, filepath.Join(dir, "GAME.JSON"))
	}
	if len(aliases) == 0 {
		t.Skip("no symlinks, case-sensitive volume")
	}
	for _, alias := range aliases {
		done := make(chan error, 1)
		go func() { _, err := Hold(real, alias); done <- err }()
		select {
		case err := <-done:
			if !errors.Is(err, ErrOneFile) {
				t.Errorf("%s: %v", alias, err)
			}
		case <-time.After(2 * time.Second):
			t.Fatalf("%s: Hold waited on itself", alias)
		}
		release, err := Hold(alias)
		if err != nil {
			t.Fatal(err)
		}
		release()
	}
}
