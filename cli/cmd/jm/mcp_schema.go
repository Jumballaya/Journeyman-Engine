package main

import (
	"errors"
	"fmt"
	"maps"
	"math"
	"slices"
	"strings"
)

// A tool's input schema is its arguments' contract, checked once as a call
// arrives (a form's fields and a setting's value are checked the same way):
// each argument given is of its type, a list's items of theirs, among its
// choices. One left out is the tool's default; bounds are the tool's to clamp.

// schemaError is why v isn't what prop says (nil when it is), worded for
// whoever gave it: "text", "a whole number", "one of game, you".
func schemaError(prop map[string]any, v any) error {
	switch prop["type"] {
	case "string":
		s, ok := v.(string)
		if !ok {
			return errors.New("text")
		}
		if choices := choicesIn(prop); choices != nil && !slices.Contains(choices, s) {
			return errors.New("one of " + strings.Join(choices, ", "))
		}
	case "boolean":
		if _, ok := v.(bool); !ok {
			return errors.New("true or false")
		}
	case "integer":
		if n, ok := number(v); !ok || n != math.Trunc(n) {
			return errors.New("a whole number")
		}
	case "number":
		if _, ok := number(v); !ok {
			return errors.New("a number")
		}
	case "array":
		items, ok := v.([]any)
		if !ok {
			return errors.New("a list")
		}
		of, _ := prop["items"].(map[string]any)
		if choices := choicesIn(prop); choices != nil { // a multi-select's choices are the list's (a form's files)
			of = map[string]any{"type": "string", "enum": choices}
		}
		for i, item := range items {
			if err := schemaError(of, item); err != nil {
				return fmt.Errorf("a list whose item %d is %v", i+1, err)
			}
		}
	case "object":
		fields, ok := v.(map[string]any)
		if !ok {
			return errors.New("an object")
		}
		if key, err := fieldsError(prop, fields); errors.Is(err, errNeeded) {
			return fmt.Errorf("an object with %s", key)
		} else if err != nil {
			return fmt.Errorf("an object whose %s is %v", key, err)
		}
	}
	return nil
}

// argsError is why a call's arguments don't fit its tool's schema: one it
// needs left out, or one given that isn't what it says. nil when they fit.
func argsError(schema map[string]any, args map[string]any) error {
	key, err := fieldsError(schema, args)
	if errors.Is(err, errNeeded) {
		return fmt.Errorf("argument %s is needed", key)
	}
	if err != nil {
		return fmt.Errorf("argument %s must be %v", key, err)
	}
	return nil
}

var errNeeded = errors.New("needed")

// fieldsError is which of an object's fields doesn't fit its schema, and why
// (errNeeded: left out). A field the schema doesn't name is its
// additionalProperties' (none: a host's own, as jm/call); null is left out.
func fieldsError(schema map[string]any, fields map[string]any) (string, error) {
	props, _ := schema["properties"].(map[string]any)
	required, _ := schema["required"].([]string)
	for _, key := range required {
		if fields[key] == nil {
			return key, errNeeded
		}
	}
	rest, _ := schema["additionalProperties"].(map[string]any)
	for _, key := range slices.Sorted(maps.Keys(fields)) {
		prop, declared := props[key].(map[string]any)
		if !declared {
			prop = rest
		}
		if prop == nil || fields[key] == nil {
			continue
		}
		if err := schemaError(prop, fields[key]); err != nil {
			return key, err
		}
	}
	return "", nil
}

// choicesIn are a field's choices: its enum, its oneOf's consts, a form's
// resources; nil when any text will do.
func choicesIn(prop map[string]any) []string {
	var out []string
	for _, c := range choicesOf(prop) {
		out = append(out, c.value)
	}
	if enum, ok := prop["enum"].([]string); ok {
		out = enum
	}
	return out
}

func number(v any) (float64, bool) {
	switch n := v.(type) {
	case float64:
		return n, true
	case int:
		return float64(n), true
	}
	return 0, false
}

// undeclaredArgs fails a tool reading an argument its schema doesn't declare
// (jm's tests set it): the schema hosts are shown is the whole contract.
var undeclaredArgs = false

// read is a call's argument key, as a tool reads it.
func (a toolArgs) read(key string) any {
	if schema, ok := a[schemaKey].(map[string]any); ok && undeclaredArgs {
		if props, _ := schema["properties"].(map[string]any); props[key] == nil {
			panic(fmt.Sprintf("a tool reads argument %q, which its input schema doesn't declare", key))
		}
	}
	return a[key]
}

const schemaKey = "jm/schema" // where a call's tool schema travels in its arguments, for read
