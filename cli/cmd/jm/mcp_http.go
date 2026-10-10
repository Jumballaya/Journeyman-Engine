package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"mime"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strings"
	"time"
)

// jm mcp --http: the same server over MCP's streamable HTTP transport, which
// ChatGPT apps (and other remote clients) use. Each POST is one JSON-RPC
// message; a request gets its response as JSON, a notification a 202. One
// game project per server: the folder jm runs in.
//
// The tools build, run and drive the game, so the server only answers at a
// secret path (a new one each start), and never to a web page: a request a
// browser sends from another site carries its Origin and is refused, and with
// no CORS headers a page can't read an answer either.
func serveMCPHTTP(addr string, allowOrigins []string) error {
	server := newMCPServer()
	defer server.stopDriver()
	path := "/mcp/" + newSessionID()

	root, _ := os.Getwd()
	fmt.Fprintf(os.Stderr, "jm mcp: serving %s at http://%s%s\n", root, addr, path)
	fmt.Fprintln(os.Stderr, "The path is the password: share it only with your MCP client.")
	fmt.Fprintln(os.Stderr, "For ChatGPT: put it behind HTTPS (e.g. `cloudflared tunnel --url http://"+addr+"` or `ngrok http "+addr+"`),")
	fmt.Fprintln(os.Stderr, "then in ChatGPT: Settings > Apps & Connectors > Create, with the tunnel's URL + "+path+".")
	srv := &http.Server{Addr: addr, Handler: mcpHTTPHandler(server, path, allowOrigins), ReadHeaderTimeout: 10 * time.Second}
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

// mcpHTTPHandler answers MCP at path and nowhere else. A request with an
// Origin must come from this machine (or an origin host allowed by name).
func mcpHTTPHandler(server *mcpServer, path string, allowOrigins []string) http.Handler {
	session := newSessionID()
	allowed := map[string]bool{"localhost": true, "127.0.0.1": true, "::1": true}
	for _, host := range allowOrigins {
		allowed[strings.ToLower(host)] = true
	}

	mux := http.NewServeMux()
	mux.HandleFunc(path, func(w http.ResponseWriter, r *http.Request) {
		if origin := r.Header.Get("Origin"); origin != "" {
			u, err := url.Parse(origin)
			if err != nil || !allowed[strings.ToLower(u.Hostname())] {
				http.Error(w, "origin not allowed: "+origin, http.StatusForbidden)
				return
			}
		}
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
		if mt, _, err := mime.ParseMediaType(r.Header.Get("Content-Type")); err != nil || mt != "application/json" {
			http.Error(w, "send JSON-RPC as application/json", http.StatusUnsupportedMediaType)
			return
		}
		body, err := io.ReadAll(io.LimitReader(r.Body, 64<<20))
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		reply, ok := server.respond(body)
		if !ok {
			w.WriteHeader(http.StatusAccepted) // a notification: nothing to answer
			return
		}
		var request struct {
			Method string `json:"method"`
		}
		if json.Unmarshal(body, &request) == nil && request.Method == "initialize" {
			w.Header().Set("Mcp-Session-Id", session)
		}
		writeRPC(w, reply)
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}
		fmt.Fprintln(w, "Journeyman MCP server: POST JSON-RPC to the path jm mcp printed.")
	})
	return mux
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
