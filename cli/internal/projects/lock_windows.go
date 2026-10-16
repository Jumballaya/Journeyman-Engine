package projects

import (
	"os"
	"syscall"
	"unsafe"
)

var lockFileEx = syscall.NewLazyDLL("kernel32.dll").NewProc("LockFileEx")

// lock blocks until this process alone holds f: LockFileEx over every byte,
// as platform::FileLock does.
func lock(f *os.File) error {
	const exclusive = 2 // LOCKFILE_EXCLUSIVE_LOCK
	var overlapped syscall.Overlapped
	ok, _, err := lockFileEx.Call(f.Fd(), exclusive, 0, 0xFFFFFFFF, 0xFFFFFFFF, uintptr(unsafe.Pointer(&overlapped)))
	if ok == 0 {
		return err
	}
	return nil
}
