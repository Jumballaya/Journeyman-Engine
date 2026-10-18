package download

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestArchivePathsCantEscape(t *testing.T) {
	if err := WriteFile(t.TempDir(), "../evil", strings.NewReader("x"), 0o644); err == nil {
		t.Fatal("wrote outside the folder")
	}
}

// A download stops when its context ends (an MCP call the client cancelled), not when the file is in.
func TestADownloadStopsWhenCancelled(t *testing.T) {
	served := make(chan struct{})
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.(http.Flusher).Flush()
		close(served)
		<-r.Context().Done() // the rest never comes
	}))
	defer server.Close()
	ctx, cancel := context.WithCancel(context.Background())
	go func() { <-served; cancel() }()
	done := make(chan error, 1)
	go func() { _, err := Get(ctx, server.URL); done <- err }()
	select {
	case err := <-done:
		if err == nil {
			t.Error("a cancelled download succeeded")
		}
	case <-time.After(5 * time.Second):
		t.Fatal("the download went on after its context ended")
	}
}
