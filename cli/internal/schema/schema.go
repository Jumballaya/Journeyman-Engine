// Package schema reads the engine's component schema (journeyman_engine
// --schema) and checks scenes and prefabs against it: unknown components,
// unknown keys and values of the wrong kind, found at build time instead of
// as a log line at run time.
package schema

import (
	"bytes"
	"encoding/json"
	"fmt"
	"math"
	"os/exec"
	"slices"
	"sort"
	"strings"
)

// Field is one key of a component's scene JSON.
type Field struct {
	Key        string   `json:"key"`
	Kind       string   `json:"kind"` // number, integer, bool, text, vec2, vec3, color, angle, mask, choice, asset, group, stringMap, json
	Default    any      `json:"default"`
	Hint       string   `json:"hint,omitempty"`
	Min        *float64 `json:"min,omitempty"` // with max: the range, when bounded
	Max        *float64 `json:"max,omitempty"`
	Step       *float64 `json:"step,omitempty"`
	Multiline  bool     `json:"multiline,omitempty"`
	Choices    []string `json:"choices,omitempty"`
	AssetTypes []string `json:"assetTypes,omitempty"`
	Fields     []Field  `json:"fields,omitempty"` // a group's
}

// ScriptField is a component field scripts reach with new Field(component, name).
type ScriptField struct {
	Name string `json:"name"`
	Type string `json:"type"` // f32 or u32
}

type Component struct {
	Label        string        `json:"label"`
	Category     string        `json:"category"`
	Summary      string        `json:"summary"`
	Fields       []Field       `json:"fields"`
	ScriptFields []ScriptField `json:"scriptFields"`
}

type Schema struct {
	SchemaVersion int                  `json:"schemaVersion"`
	Components    map[string]Component `json:"components"`
}

// Parse reads the engine's --schema output.
func Parse(data []byte) (*Schema, error) {
	var s Schema
	if err := json.Unmarshal(data, &s); err != nil {
		return nil, fmt.Errorf("schema: %w", err)
	}
	if s.SchemaVersion != 1 {
		return nil, fmt.Errorf("schema: version %d, this jm reads version 1", s.SchemaVersion)
	}
	return &s, nil
}

// Raw runs `engine --schema` and returns its output.
func Raw(engine string) ([]byte, error) {
	var stderr bytes.Buffer
	cmd := exec.Command(engine, "--schema")
	cmd.Stderr = &stderr
	out, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("%s --schema: %w %s", engine, err, strings.TrimSpace(stderr.String()))
	}
	return out, nil
}

// FromEngine asks the engine binary for its schema.
func FromEngine(engine string) (*Schema, error) {
	out, err := Raw(engine)
	if err != nil {
		return nil, err
	}
	return Parse(out)
}

// Problem is one thing wrong in a scene or prefab file.
type Problem struct {
	File    string // the file, as given to Check
	Where   string // the entity (by name, or entities[i] / prefab) and component.key
	Message string
	Line    int // of the key it's about (1-based), 0 if unknown
	Column  int
}

func (p Problem) String() string {
	if p.Where == "" {
		return fmt.Sprintf("%s: %s", p.File, p.Message)
	}
	return fmt.Sprintf("%s: %s: %s", p.File, p.Where, p.Message)
}

// Check reports what's wrong in a scene ({"entities": [...]}) or prefab
// ({"components": {...}, "children": [...]}) file, naming the entity and key.
func (s *Schema) Check(path string, data []byte) []Problem {
	var doc map[string]any
	if err := json.Unmarshal(data, &doc); err != nil {
		return []Problem{{File: path, Message: fmt.Sprintf("not valid JSON: %v", err)}}
	}
	c := checker{schema: s, path: path}
	if entities, ok := doc["entities"].([]any); ok {
		for i, e := range entities {
			c.entry(e, fmt.Sprintf("entities[%d]", i), []any{"entities", i})
		}
	} else {
		c.entry(doc, "prefab", nil)
	}
	keys := keyPositions(data)
	for i := range c.problems {
		if pos, ok := keys[pathKey(c.at[i])]; ok {
			c.problems[i].Line, c.problems[i].Column = pos[0], pos[1]
		}
	}
	return c.problems
}

// pathKey names a place in a JSON document: its keys and indices, joined.
func pathKey(path []any) string {
	var b strings.Builder
	for _, p := range path {
		fmt.Fprintf(&b, "/%v", p)
	}
	return b.String()
}

// keyPositions maps where things are in data, by path (pathKey): each object
// key's line and column, and each array element's.
func keyPositions(data []byte) map[string][2]int {
	lineStarts := []int{0}
	for i, ch := range data {
		if ch == '\n' {
			lineStarts = append(lineStarts, i+1)
		}
	}
	at := func(offset int) [2]int {
		line := sort.SearchInts(lineStarts, offset+1) // the lines starting at or before offset
		return [2]int{line, offset - lineStarts[line-1] + 1}
	}
	type container struct {
		object bool
		path   []any
		index  int    // an array's next element
		key    string // an object's current key
		hasKey bool
	}
	positions := map[string][2]int{}
	var stack []*container
	dec := json.NewDecoder(bytes.NewReader(data))
	for {
		start := int(dec.InputOffset())
		tok, err := dec.Token()
		if err != nil {
			return positions
		}
		for start < len(data) && strings.ContainsRune(" \t\r\n,:", rune(data[start])) {
			start++ // InputOffset is before the separator
		}
		var top *container
		if len(stack) > 0 {
			top = stack[len(stack)-1]
		}
		if top != nil && top.object && !top.hasKey {
			if key, ok := tok.(string); ok {
				top.key, top.hasKey = key, true
				positions[pathKey(append(slices.Clone(top.path), key))] = at(start)
				continue
			}
		}
		if tok == json.Delim('}') || tok == json.Delim(']') {
			stack = stack[:len(stack)-1]
		} else {
			var path []any
			if top != nil {
				if top.object {
					path = append(slices.Clone(top.path), top.key)
				} else {
					path = append(slices.Clone(top.path), top.index)
					positions[pathKey(path)] = at(start)
				}
			}
			if tok == json.Delim('{') || tok == json.Delim('[') {
				stack = append(stack, &container{object: tok == json.Delim('{'), path: path})
				continue // the value ends at its closing delimiter
			}
		}
		// A value ended: its container moves on.
		if len(stack) > 0 {
			parent := stack[len(stack)-1]
			if parent.object {
				parent.hasKey = false
			} else {
				parent.index++
			}
		}
	}
}

type checker struct {
	schema   *Schema
	path     string
	problems []Problem
	at       [][]any // each problem's place in the document (pathKey)
}

func (c *checker) report(where string, at []any, format string, args ...any) {
	c.problems = append(c.problems, Problem{File: c.path, Where: where, Message: fmt.Sprintf(format, args...)})
	c.at = append(c.at, slices.Clone(at))
}

// with is path plus more steps, in a new slice.
func with(path []any, more ...any) []any { return append(slices.Clone(path), more...) }

// entry checks a scene entity or prefab: its components, overrides and children.
func (c *checker) entry(v any, where string, at []any) {
	e, ok := v.(map[string]any)
	if !ok {
		c.report(where, at, "expected an object")
		return
	}
	if name, ok := e["name"].(string); ok && name != "" {
		where = fmt.Sprintf("%q", name)
	}
	if components, ok := e["components"].(map[string]any); ok {
		c.components(components, where, with(at, "components"))
	}
	if overrides, ok := e["overrides"].(map[string]any); ok {
		for _, name := range sortedKeys(overrides) {
			if name == "children" { // by child name: {"Sword": {"SpriteComponent": {...}}}
				children, _ := overrides[name].(map[string]any)
				for _, child := range sortedKeys(children) {
					if m, ok := children[child].(map[string]any); ok {
						c.components(m, where+" > "+child, with(at, "overrides", "children", child))
					}
				}
				continue
			}
			c.component(name, overrides[name], where, with(at, "overrides", name))
		}
	}
	if children, ok := e["children"].([]any); ok {
		for i, child := range children {
			c.entry(child, fmt.Sprintf("%s > children[%d]", where, i), with(at, "children", i))
		}
	}
}

func (c *checker) components(components map[string]any, where string, at []any) {
	for _, name := range sortedKeys(components) {
		c.component(name, components[name], where, with(at, name))
	}
}

// at is the component's own key in the document.
func (c *checker) component(name string, value any, where string, at []any) {
	comp, ok := c.schema.Components[name]
	if !ok {
		c.report(where, at, "unknown component %q%s", name, suggest(name, keysOf(c.schema.Components)))
		return
	}
	obj, ok := value.(map[string]any)
	if !ok {
		c.report(where, at, "%s: expected an object", name)
		return
	}
	c.fields(comp.Fields, obj, where+" "+name, at)
}

func (c *checker) fields(fields []Field, obj map[string]any, where string, at []any) {
	if len(fields) == 0 {
		return // nothing described: anything goes
	}
	byKey := map[string]Field{}
	for _, f := range fields {
		byKey[f.Key] = f
	}
	for _, key := range sortedKeys(obj) {
		f, ok := byKey[key]
		if !ok {
			c.report(where, with(at, key), "unknown key %q%s (keys: %s)", key, suggest(key, keysOf(byKey)), strings.Join(keysOf(byKey), ", "))
			continue
		}
		if problem := f.mismatch(obj[key]); problem != "" {
			c.report(where+"."+key, with(at, key), "%s", problem)
		}
		if group, ok := obj[key].(map[string]any); ok && f.Kind == "group" {
			c.fields(f.Fields, group, where+"."+key, with(at, key))
		}
	}
}

// mismatch says why v can't be this field's value, or "" if it can.
func (f Field) mismatch(v any) string {
	if v == nil {
		return "" // absent
	}
	number := func(x any) bool { _, ok := x.(float64); return ok }
	numbers := func(n ...int) bool {
		a, ok := v.([]any)
		return ok && slices.Contains(n, len(a)) && !slices.ContainsFunc(a, func(x any) bool { return !number(x) })
	}
	want := ""
	switch f.Kind {
	case "number", "angle":
		if !number(v) {
			want = "a number"
		}
	case "integer":
		if x, ok := v.(float64); !ok || x != math.Trunc(x) {
			want = "a whole number"
		}
	case "mask":
		if x, ok := v.(float64); !ok || x != math.Trunc(x) || x < 0 || x > math.MaxUint32 {
			want = "a layer mask (a whole number from 0 to 4294967295)"
		}
	case "bool":
		if _, ok := v.(bool); !ok {
			want = "true or false"
		}
	case "text", "asset":
		if _, ok := v.(string); !ok {
			want = "a string"
		}
	case "vec2":
		if !numbers(2) {
			want = "[x, y]"
		}
	case "vec3":
		if !numbers(3) {
			want = "[x, y, z]"
		}
	case "color":
		if s, ok := v.(string); !(ok && strings.HasPrefix(s, "#")) && !numbers(3, 4) {
			want = `[r, g, b, a] (0..1) or "#rrggbb"`
		}
	case "choice":
		if s, ok := v.(string); !ok || !slices.Contains(f.Choices, s) {
			want = "one of " + strings.Join(f.Choices, ", ")
		}
	case "group", "stringMap":
		if _, ok := v.(map[string]any); !ok {
			want = "an object"
		}
	}
	if want == "" {
		return ""
	}
	got, _ := json.Marshal(v)
	return fmt.Sprintf("expected %s, got %s", want, got)
}

// Suggest is ` (did you mean "X"?)` for the name in names closest to name, or
// "" if none is close.
func Suggest(name string, names []string) string { return suggest(name, names) }

// suggest is ` (did you mean "X"?)` for the closest name, if one is close.
func suggest(name string, names []string) string {
	best, bestDist := "", 4
	for _, n := range names {
		d := distance(strings.ToLower(name), strings.ToLower(n))
		if strings.HasPrefix(strings.ToLower(n), strings.ToLower(name)) {
			d = min(d, 1) // "BoxCollider" for "BoxColliderComponent"
		}
		if d < bestDist {
			best, bestDist = n, d
		}
	}
	if best == "" {
		return ""
	}
	return fmt.Sprintf(" (did you mean %q?)", best)
}

// distance is the Levenshtein edit distance.
func distance(a, b string) int {
	prev := make([]int, len(b)+1)
	for j := range prev {
		prev[j] = j
	}
	for i := 1; i <= len(a); i++ {
		cur := make([]int, len(b)+1)
		cur[0] = i
		for j := 1; j <= len(b); j++ {
			cost := 1
			if a[i-1] == b[j-1] {
				cost = 0
			}
			cur[j] = min(prev[j]+1, cur[j-1]+1, prev[j-1]+cost)
		}
		prev = cur
	}
	return prev[len(b)]
}

func keysOf[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func sortedKeys(m map[string]any) []string { return keysOf(m) }
