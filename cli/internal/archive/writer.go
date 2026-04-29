// Package archive emits and reads the Journeyman Engine archive format
// ("JMA1"). Layout: a fixed 32-byte header, a payload section of concatenated
// raw blobs, and a JSON resolver section that maps source paths to per-entry
// (offset, size, type, metadata).
//
// The format is documented in the project-E plan (Locked design decisions →
// Archive format on-disk). This package is the canonical Go-side writer; the
// engine has a parallel C++ reader at engine/core/assets/Archive.{hpp,cpp}.
package archive

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
)

// Magic = ASCII 'J','M','A','1' read little-endian as a u32.
const Magic uint32 = 0x31414D4A

// Version of the on-disk format. Bumping this requires a re-pack.
const Version uint32 = 1

// HeaderSize is the fixed byte length of the archive header.
const HeaderSize int64 = 32

// AssetEntry is one logical entry in the archive: the source path the engine
// looks up, its declared type, optional per-type metadata, and the raw payload
// bytes.
type AssetEntry struct {
	SourcePath string
	Type       string
	Metadata   map[string]interface{}
	Payload    []byte
}

// resolverEntry is the on-disk shape of a single resolver record. Offsets are
// relative to the start of the payload section, NOT absolute file offsets.
type resolverEntry struct {
	Offset   uint64                 `json:"offset"`
	Size     uint64                 `json:"size"`
	Type     string                 `json:"type"`
	Metadata map[string]interface{} `json:"metadata"`
}

// WriteArchive serializes entries to out in the JMA1 format. Entries are
// written to the payload section in the given order; the resolver records the
// (offset, size) of each blob within the payload section.
//
// Caller is responsible for canonicalizing source paths beforehand (forward
// slashes, no `./` segments) so that engine-side lookups using lexically
// normalized keys hit the resolver entry.
func WriteArchive(out io.Writer, entries []AssetEntry) error {
	resolver := make(map[string]resolverEntry, len(entries))
	var payload []byte
	var offset uint64

	for _, e := range entries {
		if _, exists := resolver[e.SourcePath]; exists {
			return fmt.Errorf("archive: duplicate source path %q", e.SourcePath)
		}
		size := uint64(len(e.Payload))
		meta := e.Metadata
		if meta == nil {
			meta = map[string]interface{}{}
		}
		resolver[e.SourcePath] = resolverEntry{
			Offset:   offset,
			Size:     size,
			Type:     e.Type,
			Metadata: meta,
		}
		payload = append(payload, e.Payload...)
		offset += size
	}

	resolverJSON, err := json.Marshal(resolver)
	if err != nil {
		return fmt.Errorf("archive: marshal resolver: %w", err)
	}

	payloadOffset := uint64(HeaderSize)
	payloadSize := uint64(len(payload))
	resolverOffset := payloadOffset + payloadSize

	header := make([]byte, HeaderSize)
	binary.LittleEndian.PutUint32(header[0:4], Magic)
	binary.LittleEndian.PutUint32(header[4:8], Version)
	binary.LittleEndian.PutUint64(header[8:16], payloadOffset)
	binary.LittleEndian.PutUint64(header[16:24], payloadSize)
	binary.LittleEndian.PutUint64(header[24:32], resolverOffset)

	if _, err := out.Write(header); err != nil {
		return fmt.Errorf("archive: write header: %w", err)
	}
	if len(payload) > 0 {
		if _, err := out.Write(payload); err != nil {
			return fmt.Errorf("archive: write payload: %w", err)
		}
	}
	if _, err := out.Write(resolverJSON); err != nil {
		return fmt.Errorf("archive: write resolver: %w", err)
	}
	return nil
}
