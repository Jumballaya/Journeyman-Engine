package filelock

import (
	"errors"
	"os"
	"path/filepath"
	"runtime"
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

// held starts Hold(path) and says on the channel once it has it; it lets go when done closes.
func held(t *testing.T, path string, done <-chan struct{}) <-chan struct{} {
	got := make(chan struct{})
	go func() {
		release, err := Hold(path)
		if err != nil {
			t.Error(err)
			close(got)
			return
		}
		close(got)
		<-done
		release()
	}()
	return got
}

func waiting(c <-chan struct{}) bool {
	select {
	case <-c:
		return false
	case <-time.After(150 * time.Millisecond):
		return true
	}
}

// A lock file replaced while someone waits on it: the waiter takes the one
// there now, so it still waits for that one's holder.
func TestWaitersOnAReplacedLockMoveToTheCurrentOne(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("Windows keeps a held file from being replaced or deleted")
	}
	path := filepath.Join(t.TempDir(), "files")
	firstDone, secondDone, waiterDone := make(chan struct{}), make(chan struct{}), make(chan struct{})
	defer close(waiterDone)
	<-held(t, path, firstDone)
	waiter := held(t, path, waiterDone)
	if !waiting(waiter) {
		t.Fatal("took a held lock")
	}
	name, _ := lockFile(path)
	os.WriteFile(name+".new", nil, 0o644)
	if err := os.Rename(name+".new", name); err != nil {
		t.Fatal(err)
	}
	<-held(t, path, secondDone) // the replacement's holder
	close(firstDone)
	if !waiting(waiter) {
		t.Fatal("the waiter took the lock left behind while the current one is held")
	}
	close(secondDone)
	<-waiter
}

// A lock file deleted while held: the next holder makes a new one.
func TestADeletedLockIsMadeAgain(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("Windows keeps a held file from being replaced or deleted")
	}
	path := filepath.Join(t.TempDir(), "files")
	firstDone, waiterDone := make(chan struct{}), make(chan struct{})
	defer close(waiterDone)
	<-held(t, path, firstDone)
	waiter := held(t, path, waiterDone)
	name, _ := lockFile(path)
	if err := os.Remove(name); err != nil {
		t.Fatal(err)
	}
	close(firstDone)
	<-waiter
	if _, err := os.Stat(name); err != nil {
		t.Errorf("no lock file now: %v", err)
	}
}
