// Package jsonfmt writes JSON in the one layout the editor and jm share, so a
// file reads the same whoever wrote it and a small change is a small diff:
//
//   - two-space indent, keys in the order they came;
//   - arrays of scalars on one line when that line is at most 72 bytes
//     ("position": [0, 20, 1]), one element per line otherwise;
//   - {} and [] for empty containers;
//   - numbers rounded to six decimals, whole ones written as integers (a value
//     that passed through a float, 0.4000000059604645, reads as typed: 0.4),
//     others in their shortest form, as nlohmann::json writes them;
//   - strings escaped minimally (", \, control characters), UTF-8 as is;
//   - a newline at the end.
//
// The editor's C++ writer (editor/JsonFormat.cpp) produces the same bytes;
// both are tested against testdata/.
package jsonfmt

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"strconv"
	"strings"
)

const inlineWidth = 72

// Format rewrites a JSON document in the shared layout.
func Format(data []byte) ([]byte, error) {
	value, err := parse(data)
	if err != nil {
		return nil, err
	}
	var out strings.Builder
	write(&out, value, 0)
	out.WriteByte('\n')
	return []byte(out.String()), nil
}

// FormatKeeping is Format for an edited document (decoded and re-encoded,
// which loses key order): each object keeps its keys in the order they had
// in previous, with keys it didn't have after them.
func FormatKeeping(data, previous []byte) ([]byte, error) {
	edited, err := parse(data)
	if err != nil {
		return nil, err
	}
	if before, err := parse(previous); err == nil {
		keepOrder(edited, before)
	}
	var out strings.Builder
	write(&out, edited, 0)
	out.WriteByte('\n')
	return []byte(out.String()), nil
}

func parse(data []byte) (any, error) {
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	value, err := decode(dec)
	if err != nil {
		return nil, err
	}
	if _, err := dec.Token(); err != io.EOF {
		return nil, fmt.Errorf("unexpected data after the JSON value")
	}
	return value, nil
}

func keepOrder(value, before any) {
	switch v := value.(type) {
	case *object:
		old, ok := before.(*object)
		if !ok {
			return
		}
		keys, values := []string{}, []any{}
		for j, key := range old.keys { // the old order first
			if i := indexOf(v.keys, key); i >= 0 {
				keepOrder(v.values[i], old.values[j])
				keys, values = append(keys, key), append(values, v.values[i])
			}
		}
		for i, key := range v.keys { // then new keys
			if indexOf(old.keys, key) < 0 {
				keys, values = append(keys, key), append(values, v.values[i])
			}
		}
		v.keys, v.values = keys, values
	case []any:
		if old, ok := before.([]any); ok {
			for i := range v {
				if i < len(old) {
					keepOrder(v[i], old[i])
				}
			}
		}
	}
}

// An object keeps its keys' order.
type object struct {
	keys   []string
	values []any
}

func decode(dec *json.Decoder) (any, error) {
	tok, err := dec.Token()
	if err != nil {
		return nil, err
	}
	switch t := tok.(type) {
	case json.Delim:
		switch t {
		case '{':
			obj := &object{}
			for dec.More() {
				keyTok, err := dec.Token()
				if err != nil {
					return nil, err
				}
				value, err := decode(dec)
				if err != nil {
					return nil, err
				}
				key := keyTok.(string)
				if i := indexOf(obj.keys, key); i >= 0 { // a repeated key: the last wins, in the first's place
					obj.values[i] = value
					continue
				}
				obj.keys, obj.values = append(obj.keys, key), append(obj.values, value)
			}
			_, err := dec.Token() // }
			return obj, err
		case '[':
			list := []any{}
			for dec.More() {
				value, err := decode(dec)
				if err != nil {
					return nil, err
				}
				list = append(list, value)
			}
			_, err := dec.Token() // ]
			return list, err
		}
	}
	return tok, nil // string, json.Number, bool, nil
}

func indexOf(keys []string, key string) int {
	for i, k := range keys {
		if k == key {
			return i
		}
	}
	return -1
}

func write(out *strings.Builder, value any, depth int) {
	switch v := value.(type) {
	case *object:
		if len(v.keys) == 0 {
			out.WriteString("{}")
			return
		}
		out.WriteString("{\n")
		for i, key := range v.keys {
			indent(out, depth+1)
			out.WriteString(quote(key))
			out.WriteString(": ")
			write(out, v.values[i], depth+1)
			if i < len(v.keys)-1 {
				out.WriteByte(',')
			}
			out.WriteByte('\n')
		}
		indent(out, depth)
		out.WriteByte('}')
	case []any:
		if len(v) == 0 {
			out.WriteString("[]")
			return
		}
		if line, ok := inline(v); ok {
			out.WriteString(line)
			return
		}
		out.WriteString("[\n")
		for i, item := range v {
			indent(out, depth+1)
			write(out, item, depth+1)
			if i < len(v)-1 {
				out.WriteByte(',')
			}
			out.WriteByte('\n')
		}
		indent(out, depth)
		out.WriteByte(']')
	default:
		out.WriteString(scalar(v))
	}
}

// inline is an array of scalars on one line, if it fits.
func inline(list []any) (string, bool) {
	parts := make([]string, len(list))
	for i, item := range list {
		switch item.(type) {
		case *object, []any:
			return "", false
		}
		parts[i] = scalar(item)
	}
	line := "[" + strings.Join(parts, ", ") + "]"
	return line, len(line) <= inlineWidth
}

func indent(out *strings.Builder, depth int) {
	for i := 0; i < depth; i++ {
		out.WriteString("  ")
	}
}

func scalar(v any) string {
	switch t := v.(type) {
	case nil:
		return "null"
	case bool:
		return strconv.FormatBool(t)
	case string:
		return quote(t)
	case json.Number:
		return number(string(t))
	}
	return fmt.Sprint(v)
}

// number writes a JSON number as the editor does (wholeNumbersAsIntegers, then nlohmann).
func number(text string) string {
	if !strings.ContainsAny(text, ".eE") {
		if _, err := strconv.ParseInt(text, 10, 64); err == nil {
			return text // an integer, as written
		}
		if _, err := strconv.ParseUint(text, 10, 64); err == nil {
			return text
		}
	}
	v, err := strconv.ParseFloat(text, 64)
	if err != nil {
		return text
	}
	if math.Abs(v) < 1e9 {
		v = math.Round(v*1e6) / 1e6
	}
	if math.Abs(v) < 1e15 && v == math.Floor(v) {
		return strconv.FormatInt(int64(v), 10)
	}
	return shortest(v)
}

// shortest is nlohmann::json's float format: the shortest digits that read
// back as v, with a decimal point between 1e-5 and 1e15, else an exponent.
func shortest(v float64) string {
	if v == 0 {
		return "0.0"
	}
	sign := ""
	if v < 0 {
		sign, v = "-", -v
	}
	e := strconv.FormatFloat(v, 'e', -1, 64) // d.ddde±x
	mantissa, exp, _ := strings.Cut(e, "e")
	digits := strings.Replace(mantissa, ".", "", 1)
	x, _ := strconv.Atoi(exp)
	k := len(digits)
	n := x + 1 // where the decimal point goes, counted in digits
	switch {
	case k <= n && n <= 15:
		return sign + digits + strings.Repeat("0", n-k) + ".0"
	case 0 < n && n <= 15:
		return sign + digits[:n] + "." + digits[n:]
	case -4 < n && n <= 0:
		return sign + "0." + strings.Repeat("0", -n) + digits
	}
	m := digits[:1]
	if k > 1 {
		m += "." + digits[1:]
	}
	exponent := n - 1
	expSign := "+"
	if exponent < 0 {
		expSign, exponent = "-", -exponent
	}
	return fmt.Sprintf("%s%se%s%02d", sign, m, expSign, exponent)
}

func quote(s string) string {
	var b strings.Builder
	b.WriteByte('"')
	for _, r := range s {
		switch r {
		case '"':
			b.WriteString(`\"`)
		case '\\':
			b.WriteString(`\\`)
		case '\b':
			b.WriteString(`\b`)
		case '\f':
			b.WriteString(`\f`)
		case '\n':
			b.WriteString(`\n`)
		case '\r':
			b.WriteString(`\r`)
		case '\t':
			b.WriteString(`\t`)
		default:
			if r < 0x20 {
				fmt.Fprintf(&b, `\u%04x`, r)
			} else {
				b.WriteRune(r)
			}
		}
	}
	b.WriteByte('"')
	return b.String()
}
