// Package embed appends a game archive to an engine executable (a "player"),
// producing one self-contained game binary. The engine finds the archive
// through a footer: the archive's offset (u64 LE) then "JMGAME01". The footer
// ends the file, or sits before a code signature added afterwards.
package embed

import (
	"encoding/binary"
	"errors"
	"fmt"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
)

var footerMagic = []byte("JMGAME01")

const (
	machoMagic64     = 0xfeedfacf
	machoHeaderSize  = 32
	lcSegment64      = 0x19
	lcCodeSignature  = 0x1d
	machoPageSize    = 0x4000 // arm64; also a multiple of x86_64's 0x1000
	archiveAlignment = 16
)

// IsMachO reports whether the player is a Mach-O executable (needs signing on macOS).
func IsMachO(player []byte) bool {
	return len(player) >= 4 && binary.LittleEndian.Uint32(player) == machoMagic64
}

// Game returns player with archive appended. Mach-O players keep their layout
// valid for codesign: any old signature is dropped and __LINKEDIT is grown to
// cover the archive, so the result can be signed again (it must be, on Apple
// Silicon). Other formats get the archive appended as is.
func Game(player, archive []byte) ([]byte, error) {
	if IsMachO(player) {
		return machoGame(player, archive)
	}
	if len(player) >= 4 && binary.BigEndian.Uint32(player) == 0xcafebabe {
		return nil, errors.New("universal (fat) players aren't supported; use a single-architecture build")
	}
	return appendArchive(player, archive), nil
}

func appendArchive(player, archive []byte) []byte {
	pad := (archiveAlignment - len(player)%archiveAlignment) % archiveAlignment
	out := make([]byte, 0, len(player)+pad+len(archive)+16)
	out = append(out, player...)
	out = append(out, make([]byte, pad)...)
	start := uint64(len(out))
	out = append(out, archive...)
	out = binary.LittleEndian.AppendUint64(out, start)
	return append(out, footerMagic...)
}

type loadCommand struct {
	offset, size int
	cmd          uint32
}

func machoGame(player, archive []byte) ([]byte, error) {
	if len(player) < machoHeaderSize {
		return nil, errors.New("truncated Mach-O header")
	}
	bin := append([]byte(nil), player...)
	ncmds := int(binary.LittleEndian.Uint32(bin[16:]))
	sizeofcmds := int(binary.LittleEndian.Uint32(bin[20:]))
	var cmds []loadCommand
	for i, off := 0, machoHeaderSize; i < ncmds; i++ {
		if off+8 > len(bin) {
			return nil, errors.New("truncated Mach-O load commands")
		}
		cmd := binary.LittleEndian.Uint32(bin[off:])
		size := int(binary.LittleEndian.Uint32(bin[off+4:]))
		if size < 8 || off+size > len(bin) {
			return nil, errors.New("malformed Mach-O load command")
		}
		cmds = append(cmds, loadCommand{off, size, cmd})
		off += size
	}

	linkedit := -1
	for _, c := range cmds {
		if c.cmd == lcSegment64 && c.size >= 72 && string(trimZero(bin[c.offset+8:c.offset+24])) == "__LINKEDIT" {
			linkedit = c.offset
		}
	}
	if linkedit < 0 {
		return nil, errors.New("Mach-O has no __LINKEDIT segment")
	}
	linkeditOff := binary.LittleEndian.Uint64(bin[linkedit+40:])

	// Drop the old signature: it is __LINKEDIT's tail, and codesign re-adds one.
	for _, c := range cmds {
		if c.cmd != lcCodeSignature {
			continue
		}
		dataoff := int(binary.LittleEndian.Uint32(bin[c.offset+8:]))
		if dataoff > len(bin) || uint64(dataoff) < linkeditOff {
			return nil, errors.New("Mach-O code signature outside __LINKEDIT")
		}
		bin = bin[:dataoff]
		end := machoHeaderSize + sizeofcmds
		copy(bin[c.offset:end], bin[c.offset+c.size:end])
		clear(bin[end-c.size : end])
		binary.LittleEndian.PutUint32(bin[16:], uint32(ncmds-1))
		binary.LittleEndian.PutUint32(bin[20:], uint32(sizeofcmds-c.size))
		if linkedit > c.offset {
			linkedit -= c.size
		}
		break
	}

	out := appendArchive(bin, archive)
	fileSize := uint64(len(out)) - linkeditOff
	vmSize := (fileSize + machoPageSize - 1) &^ (machoPageSize - 1)
	binary.LittleEndian.PutUint64(out[linkedit+32:], vmSize)
	binary.LittleEndian.PutUint64(out[linkedit+48:], fileSize)
	return out, nil
}

func trimZero(b []byte) []byte {
	for i, c := range b {
		if c == 0 {
			return b[:i]
		}
	}
	return b
}

// Find returns the archive embedded in a game binary, searching backwards as
// the engine does (Archive.cpp).
func Find(game []byte) ([]byte, error) {
	for end := len(game); end >= 16; end-- {
		if string(game[end-8:end]) != string(footerMagic) {
			continue
		}
		start, stop := binary.LittleEndian.Uint64(game[end-16:]), uint64(end-16)
		if start <= stop && stop-start >= uint64(archive.HeaderSize) && binary.LittleEndian.Uint32(game[start:]) == archive.Magic {
			return game[start:stop], nil
		}
	}
	return nil, fmt.Errorf("no embedded game found")
}
