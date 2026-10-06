package embed

import (
	"bytes"
	"encoding/binary"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
)

// A minimal Mach-O: header, __LINKEDIT, and a code signature at the file's end.
func fakeMachO() []byte {
	const linkeditOff = 0x4000
	bin := make([]byte, linkeditOff+0x100)
	binary.LittleEndian.PutUint32(bin, machoMagic64)
	binary.LittleEndian.PutUint32(bin[16:], 2)     // ncmds
	binary.LittleEndian.PutUint32(bin[20:], 72+16) // sizeofcmds
	seg := bin[machoHeaderSize:]
	binary.LittleEndian.PutUint32(seg, lcSegment64)
	binary.LittleEndian.PutUint32(seg[4:], 72)
	copy(seg[8:], "__LINKEDIT")
	binary.LittleEndian.PutUint64(seg[32:], 0x4000)      // vmsize
	binary.LittleEndian.PutUint64(seg[40:], linkeditOff) // fileoff
	binary.LittleEndian.PutUint64(seg[48:], 0x100)       // filesize
	sig := bin[machoHeaderSize+72:]
	binary.LittleEndian.PutUint32(sig, lcCodeSignature)
	binary.LittleEndian.PutUint32(sig[4:], 16)
	binary.LittleEndian.PutUint32(sig[8:], linkeditOff+0x80) // signature = __LINKEDIT's last 0x80 bytes
	binary.LittleEndian.PutUint32(sig[12:], 0x80)
	return bin
}

func TestMachOGameDropsSignatureAndCoversArchiveWithLinkedit(t *testing.T) {
	arc := append(binary.LittleEndian.AppendUint32(nil, archive.Magic), make([]byte, 60)...)
	game, err := Game(fakeMachO(), arc)
	if err != nil {
		t.Fatal(err)
	}
	if n := binary.LittleEndian.Uint32(game[16:]); n != 1 {
		t.Fatalf("ncmds = %d, want the signature command removed", n)
	}
	seg := game[machoHeaderSize:]
	if end := binary.LittleEndian.Uint64(seg[40:]) + binary.LittleEndian.Uint64(seg[48:]); end != uint64(len(game)) {
		t.Fatalf("__LINKEDIT ends at %d, file at %d", end, len(game))
	}
	// codesign then appends a signature; the archive must still be found.
	signed := append(game, bytes.Repeat([]byte{0xAB}, 300)...)
	found, err := Find(signed)
	if err != nil || !bytes.Equal(found, arc) {
		t.Fatalf("Find = %d bytes, %v", len(found), err)
	}
}

func TestFindRejectsOverflowingFooterOffset(t *testing.T) {
	game := append(binary.LittleEndian.AppendUint64(make([]byte, 64), ^uint64(0)-8), footerMagic...)
	if _, err := Find(game); err == nil {
		t.Fatal("expected no embedded game")
	}
}
