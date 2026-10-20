package archive

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"sort"
)

// Archive is a read-only view over a JMA1 archive. Mirrors the C++
// engine/core/assets/Archive minimal surface used by the CLI's `jm run`.
type Archive struct {
	bytes         []byte
	payloadOffset uint64
	entries       map[string]resolverEntry
}

// ReadArchive validates and parses an archive from in. size must be the total
// archive byte length (typically obtained via os.File.Stat). Returns an error
// for any structural defect (bad magic, unsupported version, header
// inconsistency, malformed resolver JSON, entry bounds exceeding payload).
func ReadArchive(in io.ReaderAt, size int64) (*Archive, error) {
	if size < HeaderSize {
		return nil, fmt.Errorf("archive: file shorter than header (%d bytes)", size)
	}
	buf := make([]byte, size)
	if _, err := in.ReadAt(buf, 0); err != nil {
		return nil, fmt.Errorf("archive: read: %w", err)
	}
	magic := binary.LittleEndian.Uint32(buf[0:4])
	if magic != Magic {
		return nil, fmt.Errorf("archive: bad magic 0x%08x", magic)
	}
	version := binary.LittleEndian.Uint32(buf[4:8])
	if version != Version {
		return nil, fmt.Errorf("archive: unsupported version %d", version)
	}
	payloadOffset := binary.LittleEndian.Uint64(buf[8:16])
	payloadSize := binary.LittleEndian.Uint64(buf[16:24])
	resolverOffset := binary.LittleEndian.Uint64(buf[24:32])
	if payloadOffset != uint64(HeaderSize) {
		return nil, fmt.Errorf("archive: payload_offset (%d) != header size (%d)", payloadOffset, HeaderSize)
	}
	if payloadOffset+payloadSize != resolverOffset {
		return nil, fmt.Errorf("archive: header inconsistent (payload_offset %d + payload_size %d != resolver_offset %d)",
			payloadOffset, payloadSize, resolverOffset)
	}
	if resolverOffset > uint64(size) {
		return nil, fmt.Errorf("archive: resolver_offset (%d) past EOF (%d)", resolverOffset, size)
	}
	var entries map[string]resolverEntry
	if err := json.Unmarshal(buf[resolverOffset:], &entries); err != nil {
		return nil, fmt.Errorf("archive: resolver json: %w", err)
	}
	for k, e := range entries {
		if e.Offset > payloadSize || e.Size > payloadSize-e.Offset {
			return nil, fmt.Errorf("archive: entry %q out of bounds (offset=%d size=%d payload_size=%d)",
				k, e.Offset, e.Size, payloadSize)
		}
	}
	return &Archive{bytes: buf, payloadOffset: payloadOffset, entries: entries}, nil
}

func (a *Archive) Contains(sourcePath string) bool {
	_, ok := a.entries[sourcePath]
	return ok
}

// Read returns a copy of the entry's payload bytes. Returns an error if the
// path is not in the resolver.
func (a *Archive) Read(sourcePath string) ([]byte, error) {
	e, ok := a.entries[sourcePath]
	if !ok {
		return nil, fmt.Errorf("archive: no such entry: %q", sourcePath)
	}
	start := a.payloadOffset + e.Offset
	return bytes.Clone(a.bytes[start : start+e.Size]), nil
}

// Entries returns the resolver keys sorted lexically.
func (a *Archive) Entries() []string {
	keys := make([]string, 0, len(a.entries))
	for k := range a.entries {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}
