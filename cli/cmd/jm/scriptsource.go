package main

import (
	"os"
	"regexp"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// A script's source as jm reads it (one lexer: what's code, what's a comment
// or inside a string), and the resources it names in calls the runtime
// resolves by name. Build checks and the game's index both read scripts so.

// scriptSource is a script's text with its comments blanked out, and its code:
// that with string literals' insides blanked too (all the same length, so offsets
// line up and give lines and columns). Checks find calls in code and read their
// literal arguments from text.
type scriptSource struct{ file, text, code string }

// inCode is whether offset is in code, not inside a string literal.
func (s scriptSource) inCode(offset int) bool { return s.code[offset] == s.text[offset] }

func newScriptSource(file, data string) scriptSource {
	text, code := blankComments(data)
	return scriptSource{file, text, code}
}

func scriptSources(man manifest.GameManifest) []scriptSource {
	var out []scriptSource
	for _, file := range man.Assets {
		if !strings.HasSuffix(file, ".ts") {
			continue
		}
		if data, err := os.ReadFile(file); err == nil {
			out = append(out, newScriptSource(file, string(data)))
		}
	}
	return out
}

// blankComments turns // and /* */ comments outside string literals into
// spaces, keeping newlines; code also blanks what's inside string literals
// (but not a template's ${...}, which is code).
func blankComments(text string) (string, string) {
	b, c := []byte(text), []byte(text)
	blank := func(i int) {
		if b[i] != '\n' {
			c[i] = ' '
		}
	}
	var open []byte // what's open, innermost last: a string's quote, or '{' in a template's ${...}
	for i := 0; i < len(b); i++ {
		inString := len(open) > 0 && open[len(open)-1] != '{'
		switch {
		case inString && b[i] == open[len(open)-1]:
			open = open[:len(open)-1]
		case inString && open[len(open)-1] == '`' && b[i] == '$' && i+1 < len(b) && b[i+1] == '{':
			open = append(open, '{')
			i++
		case inString:
			if b[i] == '\\' && i+1 < len(b) {
				blank(i)
				i++
			}
			blank(i)
		case b[i] == '"' || b[i] == '\'' || b[i] == '`':
			open = append(open, b[i])
		case len(open) > 0 && (b[i] == '{' || b[i] == '}'): // braces in a ${...}: its end is the one that closes it
			if b[i] == '{' {
				open = append(open, '{')
			} else {
				open = open[:len(open)-1]
			}
		case b[i] == '/' && i+1 < len(b) && (b[i+1] == '/' || b[i+1] == '*'):
			block := b[i+1] == '*'
			for ; i < len(b); i++ {
				if !block && b[i] == '\n' {
					break
				}
				if block && b[i] == '*' && i+1 < len(b) && b[i+1] == '/' {
					b[i], b[i+1], c[i], c[i+1] = ' ', ' ', ' ', ' '
					i++
					break
				}
				blank(i)
				if b[i] != '\n' {
					b[i] = ' '
				}
			}
		}
	}
	return string(b), string(c)
}

// at gives a diagnostic's line and column (1-based) for an offset in s.text.
func (s scriptSource) at(offset int) (int, int) {
	line := strings.Count(s.text[:offset], "\n") + 1
	return line, offset - strings.LastIndex(s.text[:offset], "\n")
}

// resourceCalls are the runtime's calls that name a resource by its file name
// (spawn("bullet")), and the kind each loads.
var resourceCalls = func() []struct {
	call *regexp.Regexp
	kind string
} {
	const skip = `(?:[^,()]|\([^()]*\))*,` // an argument before, calls in it too
	const arg = `((?:[^,()]|\([^()]*\))*)` // the one naming it: its strings ("boss" or "battle")
	calls := []struct{ pattern, kind string }{
		{`\b(?:Audio\.play|new\s+Sound|new\s+Music)\s*\(`, "audio"},
		{`\b(?:spawn|tileGrid|new\s+Projectile)\s*\(`, "prefab"},
		{`\bspawnPlayer\s*\(` + skip, "prefab"},
		{`\b(?:Scene\.load|Scene\.transition|\.goTo)\s*\(`, "scene"},
		{`\bPostEffect\.custom\s*\(`, "shader"},
		{`\bScene\.transition\s*\(` + skip + skip, "shader"},
		{`\.goTo\s*\(` + skip, "shader"},
		{`\.effect\s*\(` + skip, "shader"},
	}
	out := make([]struct {
		call *regexp.Regexp
		kind string
	}, len(calls))
	for i, c := range calls {
		out[i].call, out[i].kind = regexp.MustCompile(c.pattern+arg), c.kind
	}
	return out
}()

// scriptRef is a resource a script names by file name in a call (spawn("bullet")):
// its kind (prefab, scene, shader, audio), the name, and where. exact: the
// argument is that one literal ("pickup_" + kind names none for sure).
type scriptRef struct {
	kind, name string
	offset     int
	exact      bool
}

// onlyLiteral: one literal, no escapes (its value would differ from its text).
var onlyLiteral = regexp.MustCompile(`^\s*(?:"[^"\\]*"|'[^'\\]*'|` + "`[^`$\\\\]*`" + `)\s*$`)

// refs are the resources s names in resourceCalls written in its code, in order.
func (s scriptSource) refs() []scriptRef {
	var out []scriptRef
	for _, c := range resourceCalls {
		for at := 0; at < len(s.text); {
			m := c.call.FindStringSubmatchIndex(s.text[at:])
			if m == nil {
				break
			}
			start, argAt, arg := at+m[0], at+m[2], s.text[at+m[2]:at+m[3]]
			if !s.inCode(start) { // in a string: not a call, but one may start inside it
				at = start + 1
				continue
			}
			at = argAt // a call inside the argument is a call too
			exact := onlyLiteral.MatchString(arg)
			for _, q := range quoted.FindAllStringSubmatchIndex(arg, -1) {
				for g := 2; g < len(q); g += 2 {
					if q[g] >= 0 {
						out = append(out, scriptRef{c.kind, arg[q[g]:q[g+1]], argAt + q[g], exact})
					}
				}
			}
		}
	}
	slices.SortFunc(out, func(a, b scriptRef) int { return a.offset - b.offset })
	return out
}
