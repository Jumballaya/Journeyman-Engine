package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestMCPOverHTTPOnlyAnswersItsClient(t *testing.T) {
	server := newMCPServer()
	defer server.stopDriver()
	const path = "/mcp/secret"
	srv := httptest.NewServer(mcpHTTPHandler(server, path, []string{"tools.example"}))
	defer srv.Close()

	const initialize = `{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}`
	post := func(path, contentType, origin string) *http.Response {
		t.Helper()
		req, _ := http.NewRequest(http.MethodPost, srv.URL+path, strings.NewReader(initialize))
		req.Header.Set("Content-Type", contentType)
		if origin != "" {
			req.Header.Set("Origin", origin)
		}
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
		return resp
	}
	for _, c := range []struct {
		name, path, contentType, origin string
		status                          int
	}{
		{"without the secret", "/mcp", "application/json", "", http.StatusNotFound},
		{"with a wrong secret", "/mcp/guess", "application/json", "", http.StatusNotFound},
		{"from another site's page", path, "application/json", "https://evil.example", http.StatusForbidden},
		{"as a form a page can send", path, "text/plain", "", http.StatusUnsupportedMediaType},
		{"from a local page", path, "application/json; charset=utf-8", "http://localhost:5173", http.StatusOK},
		{"from an allowed origin", path, "application/json", "https://tools.example", http.StatusOK},
	} {
		resp := post(c.path, c.contentType, c.origin)
		if resp.StatusCode != c.status {
			t.Errorf("%s: status %d, want %d", c.name, resp.StatusCode, c.status)
		}
		if resp.Header.Get("Access-Control-Allow-Origin") != "" {
			t.Errorf("%s: sent a CORS header", c.name)
		}
	}
	resp := post(path, "application/json", "")
	if resp.StatusCode != http.StatusOK || resp.Header.Get("Mcp-Session-Id") == "" {
		t.Errorf("initialize: status %d, session %q", resp.StatusCode, resp.Header.Get("Mcp-Session-Id"))
	}
	req, _ := http.NewRequest(http.MethodOptions, srv.URL+path, nil)
	if resp, err := http.DefaultClient.Do(req); err != nil || resp.StatusCode != http.StatusMethodNotAllowed {
		t.Errorf("OPTIONS: %v %v", resp.StatusCode, err)
	}
}
