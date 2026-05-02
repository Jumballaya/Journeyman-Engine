package wasm

import (
	"reflect"
	"testing"
)

// wasmHeader is the 8-byte preamble shared by every well-formed module.
var wasmHeader = []byte{
	0x00, 0x61, 0x73, 0x6d, // \0asm
	0x01, 0x00, 0x00, 0x00, // version 1
}

// leb128 encodes a u32 as little-endian LEB128. Used to make hand-built
// wasm bytes a bit easier to read.
func leb128(v uint32) []byte {
	var out []byte
	for {
		b := byte(v & 0x7f)
		v >>= 7
		if v != 0 {
			out = append(out, b|0x80)
		} else {
			out = append(out, b)
			return out
		}
	}
}

// section builds a section: id, length-prefix, body.
func section(id byte, body []byte) []byte {
	out := []byte{id}
	out = append(out, leb128(uint32(len(body)))...)
	out = append(out, body...)
	return out
}

// nameBytes encodes a wasm `name`: LEB128 length followed by UTF-8 bytes.
func nameBytes(s string) []byte {
	out := leb128(uint32(len(s)))
	out = append(out, []byte(s)...)
	return out
}

func TestParseImportsAndExports_HappyPath(t *testing.T) {
	// Build a minimal module with:
	//   - one type:    () -> ()
	//   - one import:  env.__jmLog (function, type 0)
	//   - one func:    type 0
	//   - one export:  "run" (function index 1, since import takes index 0)
	//   - one code body: just `end`
	//
	// We don't actually need the type/func/code sections for the parser
	// under test, but we include the type and func sections so the export
	// references something realistic. The parser only inspects sections 2
	// and 7 — others are skipped.

	// Type section (id=1): vec<functype>; one functype () -> ()
	typeSection := []byte{}
	typeSection = append(typeSection, leb128(1)...) // count
	typeSection = append(typeSection, 0x60)         // functype tag
	typeSection = append(typeSection, leb128(0)...) // params count
	typeSection = append(typeSection, leb128(0)...) // results count

	// Import section (id=2): vec<import>; one import: env.__jmLog (func, type 0)
	importBody := []byte{}
	importBody = append(importBody, leb128(1)...) // count
	importBody = append(importBody, nameBytes("env")...)
	importBody = append(importBody, nameBytes("__jmLog")...)
	importBody = append(importBody, extKindFunc)  // kind = func
	importBody = append(importBody, leb128(0)...) // type-index

	// Export section (id=7): vec<export>; one export: "run" -> func index 1
	exportBody := []byte{}
	exportBody = append(exportBody, leb128(1)...) // count
	exportBody = append(exportBody, nameBytes("run")...)
	exportBody = append(exportBody, extKindFunc)  // kind = func
	exportBody = append(exportBody, leb128(1)...) // index

	mod := []byte{}
	mod = append(mod, wasmHeader...)
	mod = append(mod, section(1, typeSection)...)
	mod = append(mod, section(2, importBody)...)
	mod = append(mod, section(7, exportBody)...)

	imports, exports, err := ParseImportsAndExports(mod)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !reflect.DeepEqual(imports, []string{"__jmLog"}) {
		t.Errorf("imports: got %v, want [__jmLog]", imports)
	}
	if !reflect.DeepEqual(exports, []string{"run"}) {
		t.Errorf("exports: got %v, want [run]", exports)
	}
}

func TestParseImportsAndExports_FiltersNonFunctionImports(t *testing.T) {
	// One function import and three non-function imports (table, memory,
	// global). Only the function name should come back.

	importBody := []byte{}
	importBody = append(importBody, leb128(4)...) // count

	// 1. function import: env.__jmLog
	importBody = append(importBody, nameBytes("env")...)
	importBody = append(importBody, nameBytes("__jmLog")...)
	importBody = append(importBody, extKindFunc)
	importBody = append(importBody, leb128(0)...) // type-index

	// 2. table import: env.table — funcref + limits {min=1}
	importBody = append(importBody, nameBytes("env")...)
	importBody = append(importBody, nameBytes("table")...)
	importBody = append(importBody, extKindTable)
	importBody = append(importBody, 0x70)         // funcref
	importBody = append(importBody, 0x00)         // limits flag = min only
	importBody = append(importBody, leb128(1)...) // min

	// 3. memory import: env.memory — limits {min=1, max=2}
	importBody = append(importBody, nameBytes("env")...)
	importBody = append(importBody, nameBytes("memory")...)
	importBody = append(importBody, extKindMemory)
	importBody = append(importBody, 0x01)         // limits flag = min+max
	importBody = append(importBody, leb128(1)...) // min
	importBody = append(importBody, leb128(2)...) // max

	// 4. global import: env.g — value-type i32, immutable
	importBody = append(importBody, nameBytes("env")...)
	importBody = append(importBody, nameBytes("g")...)
	importBody = append(importBody, extKindGlobal)
	importBody = append(importBody, 0x7f) // i32
	importBody = append(importBody, 0x00) // immutable

	// Export section also exercises kind filtering: one func export and
	// one global export. Only the func should come back.
	exportBody := []byte{}
	exportBody = append(exportBody, leb128(2)...)
	exportBody = append(exportBody, nameBytes("run")...)
	exportBody = append(exportBody, extKindFunc)
	exportBody = append(exportBody, leb128(0)...)
	exportBody = append(exportBody, nameBytes("g")...)
	exportBody = append(exportBody, extKindGlobal)
	exportBody = append(exportBody, leb128(0)...)

	mod := []byte{}
	mod = append(mod, wasmHeader...)
	mod = append(mod, section(2, importBody)...)
	mod = append(mod, section(7, exportBody)...)

	imports, exports, err := ParseImportsAndExports(mod)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !reflect.DeepEqual(imports, []string{"__jmLog"}) {
		t.Errorf("imports: got %v, want [__jmLog]", imports)
	}
	if !reflect.DeepEqual(exports, []string{"run"}) {
		t.Errorf("exports: got %v, want [run]", exports)
	}
}

func TestParseImportsAndExports_EmptyInput(t *testing.T) {
	if _, _, err := ParseImportsAndExports(nil); err == nil {
		t.Fatal("expected error on nil input, got nil")
	}
	if _, _, err := ParseImportsAndExports([]byte{}); err == nil {
		t.Fatal("expected error on empty input, got nil")
	}
}

func TestParseImportsAndExports_BadMagic(t *testing.T) {
	bad := []byte{0xde, 0xad, 0xbe, 0xef, 0x01, 0x00, 0x00, 0x00}
	if _, _, err := ParseImportsAndExports(bad); err == nil {
		t.Fatal("expected error on bad magic, got nil")
	}
}

func TestParseImportsAndExports_BadVersion(t *testing.T) {
	bad := []byte{0x00, 0x61, 0x73, 0x6d, 0x02, 0x00, 0x00, 0x00}
	if _, _, err := ParseImportsAndExports(bad); err == nil {
		t.Fatal("expected error on unsupported version, got nil")
	}
}

func TestParseImportsAndExports_TruncatedHeader(t *testing.T) {
	// Magic but no version
	bad := []byte{0x00, 0x61, 0x73, 0x6d}
	if _, _, err := ParseImportsAndExports(bad); err == nil {
		t.Fatal("expected error on truncated header, got nil")
	}
}

func TestParseImportsAndExports_NoSections(t *testing.T) {
	// Just the header — both lists should be empty, no error.
	imports, exports, err := ParseImportsAndExports(wasmHeader)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(imports) != 0 {
		t.Errorf("imports: got %v, want empty", imports)
	}
	if len(exports) != 0 {
		t.Errorf("exports: got %v, want empty", exports)
	}
}

func TestParseImportsAndExports_TruncatedSection(t *testing.T) {
	// Section header claims 100 bytes; supply only 2.
	bad := []byte{}
	bad = append(bad, wasmHeader...)
	bad = append(bad, 0x02)         // import section id
	bad = append(bad, leb128(100)...) // claim 100 bytes
	bad = append(bad, 0x00, 0x00)   // only 2 actual bytes

	if _, _, err := ParseImportsAndExports(bad); err == nil {
		t.Fatal("expected error on truncated section, got nil")
	}
}
