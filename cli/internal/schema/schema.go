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

// Check reports what's wrong in a scene ({"entities": [...]}) or prefab
// ({"components": {...}, "children": [...]}) file, one line each, naming the
// file, the entity and the key.
func (s *Schema) Check(path string, data []byte) []string {
	var doc map[string]any
	if err := json.Unmarshal(data, &doc); err != nil {
		return []string{fmt.Sprintf("%s: not valid JSON: %v", path, err)}
	}
	c := checker{schema: s, path: path}
	if entities, ok := doc["entities"].([]any); ok {
		for i, e := range entities {
			c.entry(e, fmt.Sprintf("entities[%d]", i))
		}
	} else {
		c.entry(doc, "prefab")
	}
	return c.problems
}

type checker struct {
	schema   *Schema
	path     string
	problems []string
}

func (c *checker) report(where, format string, args ...any) {
	c.problems = append(c.problems, fmt.Sprintf("%s: %s: %s", c.path, where, fmt.Sprintf(format, args...)))
}

// entry checks a scene entity or prefab: its components, overrides and children.
func (c *checker) entry(v any, where string) {
	e, ok := v.(map[string]any)
	if !ok {
		c.report(where, "expected an object")
		return
	}
	if name, ok := e["name"].(string); ok && name != "" {
		where = fmt.Sprintf("%q", name)
	}
	if components, ok := e["components"].(map[string]any); ok {
		c.components(components, where)
	}
	if overrides, ok := e["overrides"].(map[string]any); ok {
		for _, name := range sortedKeys(overrides) {
			if name == "children" { // by child name: {"Sword": {"SpriteComponent": {...}}}
				children, _ := overrides[name].(map[string]any)
				for _, child := range sortedKeys(children) {
					if m, ok := children[child].(map[string]any); ok {
						c.components(m, where+" > "+child)
					}
				}
				continue
			}
			c.component(name, overrides[name], where)
		}
	}
	if children, ok := e["children"].([]any); ok {
		for i, child := range children {
			c.entry(child, fmt.Sprintf("%s > children[%d]", where, i))
		}
	}
}

func (c *checker) components(components map[string]any, where string) {
	for _, name := range sortedKeys(components) {
		c.component(name, components[name], where)
	}
}

func (c *checker) component(name string, value any, where string) {
	comp, ok := c.schema.Components[name]
	if !ok {
		c.report(where, "unknown component %q%s", name, suggest(name, keysOf(c.schema.Components)))
		return
	}
	obj, ok := value.(map[string]any)
	if !ok {
		c.report(where, "%s: expected an object", name)
		return
	}
	c.fields(comp.Fields, obj, where+" "+name)
}

func (c *checker) fields(fields []Field, obj map[string]any, where string) {
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
			c.report(where, "unknown key %q%s (keys: %s)", key, suggest(key, keysOf(byKey)), strings.Join(keysOf(byKey), ", "))
			continue
		}
		if problem := f.mismatch(obj[key]); problem != "" {
			c.report(where+"."+key, "%s", problem)
		}
		if group, ok := obj[key].(map[string]any); ok && f.Kind == "group" {
			c.fields(f.Fields, group, where+"."+key)
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
