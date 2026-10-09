package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// runSession runs a whole multiplayer session on this machine (jm run
// --peers N): a dedicated server and N games joining it, for net.topology
// "server" or any game with a net.server block (a matchmaker, say); else N
// p2p games with the first hosting. Ends when the games have.
func runSession(g gameToRun, opts runOptions) error {
	topology, _ := g.man.Net["topology"].(string)
	serverBlock, hasServer := g.man.Net["server"].(map[string]interface{})
	dedicated := topology != "p2p" || hasServer
	port := opts.port
	if port == 0 {
		port = 7777
		if p, ok := g.man.Net["port"].(float64); ok && p > 0 {
			port = int(p)
		}
		if p, ok := serverBlock["port"].(float64); ok && p > 0 && dedicated {
			port = int(p)
		}
	}
	engine, err := resolveEnginePath(g.man.EnginePath, g.manifestPath)
	if err != nil {
		return fmt.Errorf("engine binary not found: %w", err)
	}
	address := fmt.Sprintf("127.0.0.1:%d", port)

	var procs []*exec.Cmd
	var output sync.WaitGroup
	start := func(label, exe string, env []string) (*exec.Cmd, error) {
		cmd := exec.Command(exe, g.target)
		cmd.Env = env
		stdout, _ := cmd.StdoutPipe()
		stderr, _ := cmd.StderrPipe()
		if err := cmd.Start(); err != nil {
			return nil, fmt.Errorf("%s: %w", label, err)
		}
		for _, r := range []io.Reader{stdout, stderr} {
			output.Add(1)
			go func(r io.Reader) {
				defer output.Done()
				lines := bufio.NewScanner(r)
				for lines.Scan() {
					fmt.Printf("[%s] %s\n", label, lines.Text())
				}
			}(r)
		}
		procs = append(procs, cmd)
		return cmd, nil
	}
	stopAll := func() {
		for _, p := range procs {
			if p.Process != nil && p.ProcessState == nil {
				p.Process.Kill()
			}
		}
	}
	interrupt := make(chan os.Signal, 1)
	signal.Notify(interrupt, os.Interrupt)
	defer signal.Stop(interrupt)
	go func() {
		if _, ok := <-interrupt; ok {
			stopAll()
		}
	}()

	var server *exec.Cmd
	if dedicated {
		exe, err := resolveServerPath(g.man.EnginePath, g.manifestPath)
		if err != nil {
			return err
		}
		fmt.Printf("Session: server on UDP %d and %d games joining it\n", port, opts.peers)
		if server, err = start("server", exe, peerEnv(0, "server", append(opts.netEnv(), fmt.Sprintf("JM_NET_PORT=%d", port)))); err != nil {
			return err
		}
		time.Sleep(300 * time.Millisecond) // listening before anyone dials
	} else {
		fmt.Printf("Session: %d games, peer1 hosting on UDP %d (p2p)\n", opts.peers, port)
	}

	var games []*exec.Cmd
	for i := 1; i <= opts.peers; i++ {
		label := fmt.Sprintf("peer%d", i)
		extra := append(opts.netEnv(), fmt.Sprintf("JM_WINDOW_POS=%d,%d", 40+(i-1)*80, 60+(i-1)*60))
		if os.Getenv("JM_NET_NAME") == "" {
			extra = append(extra, fmt.Sprintf("JM_NET_NAME=Player %d", i))
		}
		if !dedicated && i == 1 {
			extra = append(extra, "JM_NET_HOST=1", fmt.Sprintf("JM_NET_PORT=%d", port))
		} else {
			extra = append(extra, "JM_NET_JOIN="+address)
		}
		cmd, err := start(label, engine, peerEnv(i, label, extra))
		if err != nil {
			stopAll()
			return err
		}
		games = append(games, cmd)
		if !dedicated && i == 1 {
			time.Sleep(300 * time.Millisecond)
		}
	}

	var failed error
	for i, game := range games {
		if err := game.Wait(); err != nil && failed == nil {
			failed = fmt.Errorf("peer%d: %w", i+1, err)
		}
	}
	if server != nil && server.ProcessState == nil {
		server.Process.Signal(os.Interrupt)
		done := make(chan struct{})
		go func() { server.Wait(); close(done) }()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
			server.Process.Kill()
			<-done
		}
	}
	output.Wait()
	return failed
}

// peerEnv is this process's environment for one process of a session:
// outputs the JM_* variables name get a per-peer place, so peers don't
// overwrite each other's captures, dumps, traces, saves or error files.
func peerEnv(index int, label string, extra []string) []string {
	var env []string
	saveDir := ".jm-save"
	for _, kv := range os.Environ() {
		key, value, _ := strings.Cut(kv, "=")
		switch key {
		case "JM_CAPTURE_DIR", "JM_DUMP_DIR":
			kv = key + "=" + filepath.Join(value, label)
		case "JM_NET_TRACE", "JM_DRIVE_RECORD":
			kv = key + "=" + suffixed(value, label)
		case "JM_ERRORS":
			if value != "-" {
				kv = key + "=" + suffixed(value, label)
			}
		case "JM_SAVE_DIR":
			saveDir = value
			continue
		case "JM_INPUT_REPLAY":
			if index == 0 {
				continue // the server has no keyboard
			}
			kv = key + "=" + strings.ReplaceAll(value, "{peer}", label)
		case "JM_NET_HOST", "JM_NET_JOIN", "JM_NET_PORT":
			continue // the session decides
		case "JM_EXIT_AFTER_FRAMES", "JM_CAPTURE_FRAMES", "JM_DRIVE":
			if index == 0 {
				continue // the server lasts as long as the games; it has nothing to capture or drive
			}
		}
		env = append(env, kv)
	}
	env = append(env, "JM_SAVE_DIR="+filepath.Join(saveDir, label))
	return append(env, extra...)
}

// suffixed puts `label` before a path's extension: net.jsonl -> net.peer1.jsonl.
func suffixed(path, label string) string {
	ext := filepath.Ext(path)
	return strings.TrimSuffix(path, ext) + "." + label + ext
}
