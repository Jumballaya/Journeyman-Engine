// Package download fetches files checked against a known digest and installs
// them into a cache atomically.
package download

import (
	"context"
	"fmt"
	"hash"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"time"
)

// Get fetches url; ctx's end stops it.
func Get(ctx context.Context, url string) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, fmt.Errorf("download %s: %w", url, err)
	}
	client := &http.Client{Timeout: 10 * time.Minute}
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("download %s: %w", url, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("download %s: %s", url, resp.Status)
	}
	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("download %s: %w", url, err)
	}
	return data, nil
}

// Checked fetches url and checks it against want (the digest as encode
// writes it), so a changed or corrupted file is never used.
func Checked(ctx context.Context, url string, h hash.Hash, want string, encode func([]byte) string) ([]byte, error) {
	data, err := Get(ctx, url)
	if err != nil {
		return nil, err
	}
	h.Write(data)
	if got := encode(h.Sum(nil)); got != want {
		return nil, fmt.Errorf("download %s: checksum mismatch (got %s, want %s); not using it", url, got, want)
	}
	return data, nil
}

// Install fills dst through a temporary sibling folder and renames it into
// place, so an interrupted download never leaves a half-filled entry.
func Install(dst string, fill func(tmp string) error) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	tmp, err := os.MkdirTemp(filepath.Dir(dst), filepath.Base(dst)+".partial-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	if err := fill(tmp); err != nil {
		return err
	}
	// MkdirTemp made it 0700, which the rename keeps: a cache filled by root
	// (a container image) or shared by CI users must be readable by others.
	if err := os.Chmod(tmp, 0o755); err != nil {
		return err
	}
	if err := os.Rename(tmp, dst); err != nil {
		if _, statErr := os.Stat(dst); statErr == nil {
			return nil // another jm got there first
		}
		return err
	}
	return nil
}

// WriteFile writes r to dir/rel, refusing a rel that leaves dir.
func WriteFile(dir, rel string, r io.Reader, mode os.FileMode) error {
	if !filepath.IsLocal(rel) {
		return fmt.Errorf("archive path escapes its folder: %s", rel)
	}
	path := filepath.Join(dir, rel)
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, mode|0o200)
	if err != nil {
		return err
	}
	if _, err := io.Copy(f, r); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}
