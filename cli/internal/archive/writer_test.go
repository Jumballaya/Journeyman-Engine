package archive

import (
	"bytes"
	"sort"
	"testing"
)

func TestWriteArchiveRoundTripsViaReader(t *testing.T) {
	entries := []AssetEntry{
		{
			SourcePath: "a.txt",
			Type:       "blob",
			Metadata:   map[string]interface{}{"k": "v"},
			Payload:    []byte("alpha"),
		},
		{
			SourcePath: "b.bin",
			Type:       "blob",
			Payload:    []byte{0x00, 0xFF, 0xE2, 0x9C, 0x93},
		},
		{
			SourcePath: "nested/c.json",
			Type:       "scene",
			Payload:    []byte(`{"x":1}`),
		},
	}

	var buf bytes.Buffer
	if err := WriteArchive(&buf, entries); err != nil {
		t.Fatalf("WriteArchive: %v", err)
	}

	arc, err := ReadArchive(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("ReadArchive: %v", err)
	}

	got := arc.Entries()
	want := []string{"a.txt", "b.bin", "nested/c.json"}
	sort.Strings(want)
	if len(got) != len(want) {
		t.Fatalf("entries mismatch: got %v want %v", got, want)
	}
	for i, k := range want {
		if got[i] != k {
			t.Fatalf("entry %d: got %q want %q", i, got[i], k)
		}
	}

	for _, e := range entries {
		data, err := arc.Read(e.SourcePath)
		if err != nil {
			t.Fatalf("Read %q: %v", e.SourcePath, err)
		}
		if !bytes.Equal(data, e.Payload) {
			t.Fatalf("payload mismatch for %q: got %v want %v", e.SourcePath, data, e.Payload)
		}
	}
}

func TestWriteArchiveProducesDeterministicBytes(t *testing.T) {
	entries := []AssetEntry{
		{SourcePath: "a", Type: "blob", Payload: []byte("a")},
		{SourcePath: "b", Type: "blob", Payload: []byte("bb")},
		{SourcePath: "c", Type: "blob", Payload: []byte("ccc")},
	}
	var buf1, buf2 bytes.Buffer
	if err := WriteArchive(&buf1, entries); err != nil {
		t.Fatalf("first write: %v", err)
	}
	if err := WriteArchive(&buf2, entries); err != nil {
		t.Fatalf("second write: %v", err)
	}
	if !bytes.Equal(buf1.Bytes(), buf2.Bytes()) {
		t.Fatalf("non-deterministic output: %d vs %d bytes", buf1.Len(), buf2.Len())
	}
}

func TestWriteArchiveRejectsDuplicateSourcePath(t *testing.T) {
	entries := []AssetEntry{
		{SourcePath: "dup", Type: "blob", Payload: []byte("a")},
		{SourcePath: "dup", Type: "blob", Payload: []byte("b")},
	}
	var buf bytes.Buffer
	err := WriteArchive(&buf, entries)
	if err == nil {
		t.Fatal("expected duplicate source path error, got nil")
	}
}
