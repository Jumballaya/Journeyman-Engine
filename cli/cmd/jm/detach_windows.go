package main

import (
	"os/exec"
	"strconv"
	"syscall"
)

// detached starts a process outside jm's console: closing it doesn't stop the process.
func detached() *syscall.SysProcAttr {
	return &syscall.SysProcAttr{CreationFlags: syscall.CREATE_NEW_PROCESS_GROUP | 0x00000008} // DETACHED_PROCESS
}

// killableTree makes cmd's cancel (its context ending) kill it and every
// process it started, not only it.
func killableTree(cmd *exec.Cmd) {
	cmd.Cancel = func() error {
		return exec.Command("taskkill", "/T", "/F", "/PID", strconv.Itoa(cmd.Process.Pid)).Run()
	}
}
