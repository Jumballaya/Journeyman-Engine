package jsonfmt

import (
	"encoding/json"
	"fmt"
	"slices"
	"strings"
)

// An Edit sets the value at Path (object keys and array indexes, from the
// document's top) to Value, or with Remove takes it away. An index one past
// an array's end adds to it.
type Edit struct {
	Path   []any           `json:"path"` // string keys, integer indexes (whole float64s, as JSON decodes them)
	Value  json.RawMessage `json:"value,omitempty"`
	Remove bool            `json:"remove,omitempty"`
}

// Edited is data with edits made, in the shared layout. What no edit touches
// keeps its value exactly as written there (a number's digits) and its keys'
// order; a new key goes after an object's others.
func Edited(data []byte, edits []Edit) ([]byte, error) {
	doc, err := parse(data)
	if err != nil {
		return nil, err
	}
	for _, e := range edits {
		if doc, err = edited(doc, e.Path, e); err != nil {
			return nil, fmt.Errorf("%v: %w", e.Path, err)
		}
	}
	var out strings.Builder
	write(&out, doc, 0)
	out.WriteByte('\n')
	return []byte(out.String()), nil
}

// edited is v with e made at path (from v).
func edited(v any, path []any, e Edit) (any, error) {
	if len(path) == 0 {
		if e.Remove {
			return nil, fmt.Errorf("the whole document can't be removed")
		}
		return parse(e.Value)
	}
	switch at := v.(type) {
	case *object:
		key, ok := path[0].(string)
		if !ok {
			return nil, fmt.Errorf("%v isn't a key of an object", path[0])
		}
		i := indexOf(at.keys, key)
		switch {
		case i < 0 && len(path) == 1 && e.Remove:
			return at, nil // not there: removed already
		case i < 0 && len(path) == 1:
			value, err := parse(e.Value)
			at.keys, at.values = append(at.keys, key), append(at.values, value)
			return at, err
		case i < 0:
			return nil, fmt.Errorf("no %q", key)
		case len(path) == 1 && e.Remove:
			at.keys, at.values = slices.Delete(at.keys, i, i+1), slices.Delete(at.values, i, i+1)
			return at, nil
		}
		value, err := edited(at.values[i], path[1:], e)
		at.values[i] = value
		return at, err
	case []any:
		i, ok := index(path[0])
		if !ok || i < 0 || i > len(at) || i == len(at) && (len(path) > 1 || e.Remove) {
			return nil, fmt.Errorf("%v isn't an index of a list of %d", path[0], len(at))
		}
		if len(path) == 1 && e.Remove {
			return slices.Delete(at, i, i+1), nil
		}
		if i == len(at) {
			value, err := parse(e.Value)
			return append(at, value), err
		}
		value, err := edited(at[i], path[1:], e)
		at[i] = value
		return at, err
	}
	return nil, fmt.Errorf("%v is in a value that holds none", path[0])
}

// index is a path's part as an array index: an int, or a whole float64.
func index(part any) (int, bool) {
	switch n := part.(type) {
	case int:
		return n, true
	case float64:
		return int(n), n == float64(int(n))
	}
	return 0, false
}
