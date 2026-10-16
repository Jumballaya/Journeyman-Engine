//go:build !windows

package main

import (
	"os/exec"
	"syscall"
)

// detached starts a process in its own session: closing the terminal doesn't stop it.
func detached() *syscall.SysProcAttr { return &syscall.SysProcAttr{Setsid: true} }

// killableTree makes cmd's cancel (its context ending) kill it and every
// process it started, not only it.
func killableTree(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	cmd.Cancel = func() error { return syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL) }
}
