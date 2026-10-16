package filelock

import (
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
