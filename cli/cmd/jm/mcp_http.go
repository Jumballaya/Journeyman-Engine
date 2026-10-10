package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/signal"
	"sync"
)

// jm mcp --http: the same server over MCP's streamable HTTP transport, which
// ChatGPT apps (and other remote clients) use. Each POST to /mcp is one
// JSON-RPC message; a request gets its response as JSON, a notification a
// 202. One game project per server: the folder jm runs in.
func serveMCPHTTP(addr string) error {
	server := newMCPServer(io.Discard)
	defer server.stopDriver()
	var mu sync.Mutex // the driver and the project are one at a time
	session := newSessionID()

	mux := http.NewServeMux()
	mux.HandleFunc("/mcp", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodPost:
		case http.MethodDelete:
			w.WriteHeader(http.StatusOK) // the client ends its session: nothing kept per session
			return
		default:
			// No server-initiated messages: the GET stream isn't offered.
			w.Header().Set("Allow", "POST, DELETE")
			http.Error(w, "POST JSON-RPC messages here", http.StatusMethodNotAllowed)
			return
		}
		body, err := io.ReadAll(io.LimitReader(r.Body, 64<<20))
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		var msg rpcMessage
		if err := json.Unmarshal(body, &msg); err != nil {
			writeRPC(w, rpcMessage{JSONRPC: "2.0", ID: json.RawMessage("null"), Error: &rpcError{-32700, "parse error: " + err.Error()}})
			return
		}
		if msg.ID == nil { // a notification or a response: nothing to answer
			w.WriteHeader(http.StatusAccepted)
			return
		}
		mu.Lock()
		result, rpcErr := server.handle(msg.Method, msg.Params)
		mu.Unlock()
		reply := rpcMessage{JSONRPC: "2.0", ID: msg.ID}
		if rpcErr != nil {
			reply.Error = rpcErr
		} else {
			reply.Result = result
		}
		if msg.Method == "initialize" {
			w.Header().Set("Mcp-Session-Id", session)
		}
		writeRPC(w, reply)
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		fmt.Fprintln(w, "Journeyman MCP server: POST JSON-RPC to /mcp.")
	})

	root, _ := os.Getwd()
	fmt.Fprintf(os.Stderr, "jm mcp: serving %s at http://%s/mcp\n", root, addr)
	fmt.Fprintln(os.Stderr, "For ChatGPT: put it behind HTTPS (e.g. `cloudflared tunnel --url http://"+addr+"` or `ngrok http "+addr+"`),")
	fmt.Fprintln(os.Stderr, "then in ChatGPT: Settings > Apps & Connectors > Create, with the tunnel's URL + /mcp.")
	srv := &http.Server{Addr: addr, Handler: mux}
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt)
	go func() {
		<-stop
		_ = srv.Close()
	}()
	if err := srv.ListenAndServe(); err != http.ErrServerClosed {
		return err
	}
	return nil
}

func writeRPC(w http.ResponseWriter, msg rpcMessage) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(msg)
}

func newSessionID() string {
	b := make([]byte, 16)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b)
}
