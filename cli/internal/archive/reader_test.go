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

func TestReadArchiveChecksPayloadIntegrity(t *testing.T) {
	for _, tc := range []struct {
		name, payload, entry string
		wantError            string
	}{
		{"matching checksum", "123456789", `{"offset":0,"size":9,"crc32":3421780262}`, ""},
		{"damaged payload", "123456780", `{"offset":0,"size":9,"crc32":3421780262}`, "damaged"},
		{"zero checksum mismatch", "123456789", `{"offset":0,"size":9,"crc32":0}`, "damaged"},
		{"empty payload", "", `{"offset":0,"size":0,"crc32":0}`, ""},
		{"legacy without checksum", "123456789", `{"offset":0,"size":9}`, ""},
		{"null checksum", "123456789", `{"offset":0,"size":9,"crc32":null}`, ""},
		{"entry outside payload", "123456789", `{"offset":10,"size":1,"crc32":0}`, "out of bounds"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			buf := make([]byte, HeaderSize)
			binary.LittleEndian.PutUint32(buf[0:4], Magic)
			binary.LittleEndian.PutUint32(buf[4:8], Version)
			binary.LittleEndian.PutUint64(buf[8:16], uint64(HeaderSize))
			binary.LittleEndian.PutUint64(buf[16:24], uint64(len(tc.payload)))
			binary.LittleEndian.PutUint64(buf[24:32], uint64(HeaderSize)+uint64(len(tc.payload)))
			buf = append(buf, tc.payload...)
			buf = append(buf, `{"digits.txt":`+tc.entry+`}`...)

			arc, err := ReadArchive(bytes.NewReader(buf), int64(len(buf)))
			if tc.wantError != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantError) || !strings.Contains(err.Error(), "digits.txt") {
					t.Fatalf("expected %q error naming digits.txt, got %v", tc.wantError, err)
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			data, err := arc.Read("digits.txt")
			if err != nil || string(data) != tc.payload {
				t.Fatalf("read = %q, %v; want %q", data, err, tc.payload)
			}
		})
	}
}
