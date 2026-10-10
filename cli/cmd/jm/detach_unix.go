//go:build !windows

package main

import "syscall"

// detached starts a process in its own session: closing the terminal doesn't stop it.
func detached() *syscall.SysProcAttr { return &syscall.SysProcAttr{Setsid: true} }
