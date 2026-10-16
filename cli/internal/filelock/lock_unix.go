//go:build !windows

package filelock

import (
	"os"
	"syscall"
)

// lock blocks until this process alone holds f (platform::FileLock's flock).
func lock(f *os.File) error { return syscall.Flock(int(f.Fd()), syscall.LOCK_EX) }
