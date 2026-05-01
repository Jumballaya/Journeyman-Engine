package archive

import (
	"bytes"
	"encoding/binary"
	"strings"
	"testing"
)

func TestReadArchiveRejectsBadMagic(t *testing.T) {
	buf := make([]byte, HeaderSize)
	binary.LittleEndian.PutUint32(buf[0:4], 0xDEADBEEF)
	binary.LittleEndian.PutUint32(buf[4:8], Version)
	binary.LittleEndian.PutUint64(buf[8:16], uint64(HeaderSize))
	binary.LittleEndian.PutUint64(buf[16:24], 0)
	binary.LittleEndian.PutUint64(buf[24:32], uint64(HeaderSize))

	_, err := ReadArchive(bytes.NewReader(buf), int64(len(buf)))
	if err == nil || !strings.Contains(err.Error(), "magic") {
		t.Fatalf("expected magic error, got %v", err)
	}
}

func TestReadArchiveRejectsTruncatedFile(t *testing.T) {
	buf := []byte{0x4A, 0x4D, 0x41} // 3 bytes, less than header
	_, err := ReadArchive(bytes.NewReader(buf), int64(len(buf)))
	if err == nil || !strings.Contains(err.Error(), "shorter than header") {
		t.Fatalf("expected truncation error, got %v", err)
	}
}
