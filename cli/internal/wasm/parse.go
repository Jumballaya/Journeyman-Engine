// Package wasm contains a minimal, dependency-free parser for the slices of
// the WebAssembly binary format that jm cares about — namely the import
// (section id 2) and export (section id 7) sections.
//
// It is deliberately narrow: it does not validate the full module, it does
// not decode function bodies, and it does not return type information. It
// returns the names of imported and exported FUNCTIONS only — non-function
// items (table, memory, global) are skipped. That is all the build pipeline
// needs in order to populate `.script.json` metadata.
package wasm

import (
	"errors"
	"fmt"
)

// External-kind tags as defined by the wasm core spec.
const (
	extKindFunc   = 0x00
	extKindTable  = 0x01
	extKindMemory = 0x02
	extKindGlobal = 0x03
)

// Section IDs we care about.
const (
	sectionImport = 2
	sectionExport = 7
)

// ParseImportsAndExports reads imports (section 2) and exports (section 7)
// from a wasm binary and returns the names of FUNCTION imports/exports only.
// Non-function items (table/memory/global) are ignored.
//
// Returned slices are non-nil but may be empty. Returns an error on a bad
// magic/version header or any malformed/truncated section.
func ParseImportsAndExports(wasmBytes []byte) (imports []string, exports []string, err error) {
	imports = []string{}
	exports = []string{}

	r := &reader{buf: wasmBytes}

	// Magic: \x00asm
	magic, err := r.readBytes(4)
	if err != nil {
		return nil, nil, fmt.Errorf("wasm: reading magic: %w", err)
	}
	if string(magic) != "\x00asm" {
		return nil, nil, fmt.Errorf("wasm: bad magic %x, want 0061736d", magic)
	}

	// Version: \x01\x00\x00\x00
	version, err := r.readBytes(4)
	if err != nil {
		return nil, nil, fmt.Errorf("wasm: reading version: %w", err)
	}
	if version[0] != 0x01 || version[1] != 0x00 || version[2] != 0x00 || version[3] != 0x00 {
		return nil, nil, fmt.Errorf("wasm: unsupported version %x, want 01000000", version)
	}

	for !r.atEnd() {
		sectionID, err := r.readByte()
		if err != nil {
			return nil, nil, fmt.Errorf("wasm: reading section id: %w", err)
		}
		sectionLen, err := r.readULEB128()
		if err != nil {
			return nil, nil, fmt.Errorf("wasm: reading section length: %w", err)
		}

		body, err := r.readBytes(int(sectionLen))
		if err != nil {
			return nil, nil, fmt.Errorf("wasm: reading section %d body: %w", sectionID, err)
		}

		switch sectionID {
		case sectionImport:
			imps, err := parseImportSection(body)
			if err != nil {
				return nil, nil, fmt.Errorf("wasm: import section: %w", err)
			}
			imports = imps
		case sectionExport:
			exps, err := parseExportSection(body)
			if err != nil {
				return nil, nil, fmt.Errorf("wasm: export section: %w", err)
			}
			exports = exps
		default:
			// Section we don't care about — already consumed via readBytes.
		}
	}

	return imports, exports, nil
}

// parseImportSection parses the body of an import section (id=2).
//
// Format: vec(import) where each import is:
//
//	module:name string, name:string, kind:byte, payload (kind-dependent)
func parseImportSection(body []byte) ([]string, error) {
	r := &reader{buf: body}

	count, err := r.readULEB128()
	if err != nil {
		return nil, fmt.Errorf("import count: %w", err)
	}

	out := make([]string, 0, count)
	for i := uint32(0); i < count; i++ {
		// module name (discarded)
		if _, err := r.readString(); err != nil {
			return nil, fmt.Errorf("import[%d] module: %w", i, err)
		}
		// item name
		name, err := r.readString()
		if err != nil {
			return nil, fmt.Errorf("import[%d] name: %w", i, err)
		}
		// kind
		kind, err := r.readByte()
		if err != nil {
			return nil, fmt.Errorf("import[%d] kind: %w", i, err)
		}

		// Skip kind-dependent payload.
		switch kind {
		case extKindFunc:
			// type-index: u32 LEB128
			if _, err := r.readULEB128(); err != nil {
				return nil, fmt.Errorf("import[%d] func type-index: %w", i, err)
			}
			out = append(out, name)
		case extKindTable:
			// element-type byte + limits
			if _, err := r.readByte(); err != nil {
				return nil, fmt.Errorf("import[%d] table elem-type: %w", i, err)
			}
			if err := skipLimits(r); err != nil {
				return nil, fmt.Errorf("import[%d] table limits: %w", i, err)
			}
		case extKindMemory:
			if err := skipLimits(r); err != nil {
				return nil, fmt.Errorf("import[%d] memory limits: %w", i, err)
			}
		case extKindGlobal:
			// value-type byte + mut byte
			if _, err := r.readByte(); err != nil {
				return nil, fmt.Errorf("import[%d] global value-type: %w", i, err)
			}
			if _, err := r.readByte(); err != nil {
				return nil, fmt.Errorf("import[%d] global mut: %w", i, err)
			}
		default:
			return nil, fmt.Errorf("import[%d] unknown kind 0x%02x", i, kind)
		}
	}

	if !r.atEnd() {
		return nil, fmt.Errorf("import section: %d trailing bytes after %d entries", len(body)-r.pos, count)
	}

	return out, nil
}

// parseExportSection parses the body of an export section (id=7).
//
// Format: vec(export) where each export is:
//
//	name:string, kind:byte, index:u32 LEB128
func parseExportSection(body []byte) ([]string, error) {
	r := &reader{buf: body}

	count, err := r.readULEB128()
	if err != nil {
		return nil, fmt.Errorf("export count: %w", err)
	}

	out := make([]string, 0, count)
	for i := uint32(0); i < count; i++ {
		name, err := r.readString()
		if err != nil {
			return nil, fmt.Errorf("export[%d] name: %w", i, err)
		}
		kind, err := r.readByte()
		if err != nil {
			return nil, fmt.Errorf("export[%d] kind: %w", i, err)
		}
		// index — always LEB128 regardless of kind
		if _, err := r.readULEB128(); err != nil {
			return nil, fmt.Errorf("export[%d] index: %w", i, err)
		}
		if kind == extKindFunc {
			out = append(out, name)
		}
	}

	if !r.atEnd() {
		return nil, fmt.Errorf("export section: %d trailing bytes after %d entries", len(body)-r.pos, count)
	}

	return out, nil
}

// skipLimits consumes a `limits` block: flag byte then min (and max if flag=1).
func skipLimits(r *reader) error {
	flag, err := r.readByte()
	if err != nil {
		return fmt.Errorf("limits flag: %w", err)
	}
	if _, err := r.readULEB128(); err != nil {
		return fmt.Errorf("limits min: %w", err)
	}
	if flag == 1 {
		if _, err := r.readULEB128(); err != nil {
			return fmt.Errorf("limits max: %w", err)
		}
	} else if flag != 0 {
		return fmt.Errorf("limits: unknown flag 0x%02x", flag)
	}
	return nil
}

// reader is a tiny bounded cursor over a byte slice.
type reader struct {
	buf []byte
	pos int
}

func (r *reader) atEnd() bool {
	return r.pos >= len(r.buf)
}

func (r *reader) readByte() (byte, error) {
	if r.pos >= len(r.buf) {
		return 0, errors.New("unexpected end of input")
	}
	b := r.buf[r.pos]
	r.pos++
	return b, nil
}

func (r *reader) readBytes(n int) ([]byte, error) {
	if n < 0 {
		return nil, fmt.Errorf("negative read length %d", n)
	}
	if r.pos+n > len(r.buf) {
		return nil, fmt.Errorf("unexpected end of input: want %d bytes, have %d", n, len(r.buf)-r.pos)
	}
	out := r.buf[r.pos : r.pos+n]
	r.pos += n
	return out, nil
}

// readULEB128 decodes an unsigned LEB128 integer up to 32 bits (5 bytes max).
func (r *reader) readULEB128() (uint32, error) {
	var result uint32
	var shift uint
	for i := 0; i < 5; i++ {
		b, err := r.readByte()
		if err != nil {
			return 0, fmt.Errorf("LEB128: %w", err)
		}
		// On the 5th (final) byte, only the low 4 bits are meaningful for u32.
		if i == 4 && b&0x70 != 0 {
			return 0, errors.New("LEB128: value overflows u32")
		}
		result |= uint32(b&0x7f) << shift
		if b&0x80 == 0 {
			return result, nil
		}
		shift += 7
	}
	return 0, errors.New("LEB128: too many continuation bytes")
}

// readString reads a wasm `name`: LEB128 length followed by that many UTF-8 bytes.
func (r *reader) readString() (string, error) {
	length, err := r.readULEB128()
	if err != nil {
		return "", fmt.Errorf("string length: %w", err)
	}
	bytes, err := r.readBytes(int(length))
	if err != nil {
		return "", fmt.Errorf("string body: %w", err)
	}
	return string(bytes), nil
}
