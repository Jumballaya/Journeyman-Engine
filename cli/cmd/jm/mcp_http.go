package main

import (
	"cmp"
	"crypto/rand"
	"encoding/base64"
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
	"syscall"
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

	fmt.Fprintf(os.Stderr, "jm mcp: serving %s at http://%s%s\n", cmp.Or(server.opened(), "no game yet"), addr, path)
	fmt.Fprintln(os.Stderr, "The path is the password: share it only with your MCP client.")
	fmt.Fprintln(os.Stderr, "For ChatGPT: put it behind HTTPS (e.g. `cloudflared tunnel --url http://"+addr+"` or `ngrok http "+addr+"`),")
	fmt.Fprintln(os.Stderr, "then in ChatGPT: Settings > Apps & Connectors > Create, with the tunnel's URL + "+path+".")
	srv := &http.Server{Addr: addr, Handler: mcpHTTPHandler(server, path, allowOrigins), ReadHeaderTimeout: 10 * time.Second}
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-stop
		server.inflight.cancel(nil) // its children are in their own process groups: the signal didn't reach them
		server.calls.Lock()         // and they're gone once the call running ends
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
		if rpcErr := checkHeaders(r.Header, body); rpcErr != nil {
			reply := rpcMessage{JSONRPC: "2.0", ID: json.RawMessage("null"), Error: rpcErr}
			var req struct {
				ID json.RawMessage `json:"id"`
			}
			if json.Unmarshal(body, &req) == nil && req.ID != nil {
				reply.ID = req.ID
			}
			writeRPC(w, reply, http.StatusBadRequest)
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
		writeRPC(w, reply, modernStatus(body, reply))
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

// checkHeaders is why a modern request's headers don't mirror its body
// (version, method, the tool or resource it names), else nil: legacy ones have none to check.
func checkHeaders(h http.Header, body []byte) *rpcError {
	var req struct {
		Method string          `json:"method"`
		Params json.RawMessage `json:"params"`
	}
	_ = json.Unmarshal(body, &req)
	meta, _ := metaOf(req.Params)
	if meta.Version != mcpModernVersion && h.Get("MCP-Protocol-Version") != mcpModernVersion {
		return nil
	}
	mismatch := func(header, want string) *rpcError {
		return &rpcError{Code: -32020, Message: fmt.Sprintf("Header mismatch: %s header %q doesn't match the body's %q", header, h.Values(header), want)}
	}
	for _, header := range []string{"MCP-Protocol-Version", "Mcp-Method", "Mcp-Name"} {
		if len(h.Values(header)) > 1 { // which one would a proxy route by?
			return mismatch(header, "one value")
		}
	}
	if h.Get("MCP-Protocol-Version") != meta.Version {
		return mismatch("MCP-Protocol-Version", meta.Version)
	}
	if h.Get("Mcp-Method") != req.Method {
		return mismatch("Mcp-Method", req.Method)
	}
	var named struct {
		Name string `json:"name"`
		URI  string `json:"uri"`
	}
	_ = json.Unmarshal(req.Params, &named)
	switch req.Method {
	case "tools/call", "prompts/get":
		if name, ok := headerValue(h.Get("Mcp-Name")); !ok || name != named.Name {
			return mismatch("Mcp-Name", named.Name)
		}
	case "resources/read":
		if name, ok := headerValue(h.Get("Mcp-Name")); !ok || name != named.URI {
			return mismatch("Mcp-Name", named.URI)
		}
	}
	return nil
}

// headerValue is a header's value, decoded from its =?base64?…?= form; false: missing or malformed.
func headerValue(v string) (string, bool) {
	if v == "" {
		return "", false
	}
	if inner, ok := strings.CutPrefix(v, "=?base64?"); ok && strings.HasSuffix(inner, "?=") {
		data, err := base64.StdEncoding.DecodeString(strings.TrimSuffix(inner, "?="))
		return string(data), err == nil
	}
	for _, c := range v {
		if c != '\t' && (c < 0x20 || c > 0x7e) {
			return "", false
		}
	}
	return v, true
}

func writeRPC(w http.ResponseWriter, msg rpcMessage, status int) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(msg)
}

func newSessionID() string {
	b := make([]byte, 16)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b)
}
