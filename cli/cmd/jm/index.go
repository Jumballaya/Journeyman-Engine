package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"github.com/Jumballaya/Journeyman-Engine/internal/atlas"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"image"
	_ "image/jpeg"
	_ "image/png"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
)

// indexEntry is one file of a game: its kind, what it uses (the files it
// names) and what uses it, as paths from the game's folder.
type indexEntry struct {
	Path   string   `json:"path"`
	Kind   string   `json:"kind"`
	Detail string   `json:"detail,omitempty"` // "12 entities", "64×64"
	Uses   []string `json:"uses"`
	UsedBy []string `json:"usedBy"`
	Unused bool     `json:"unused,omitempty"` // nothing uses it, and it's a kind something should
}

// fileKinds name a game's files by their endings, the longest first.
var fileKinds = []struct{ suffix, kind string }{
	{".scene.json", "scene"}, {".prefab.json", "prefab"}, {".atlas.json", "atlas"}, {".golden.json", "golden"},
	{".spec.ts", "test"}, {".d.ts", ""}, {".ts", "script"}, {".json", "data"},
	{".png", "texture"}, {".jpg", "texture"}, {".jpeg", "texture"}, {".aseprite", "texture"},
	{".wav", "audio"}, {".ogg", "audio"}, {".mp3", "audio"}, {".flac", "audio"},
	{".ttf", "font"}, {".otf", "font"}, {".fnt", "font"},
	{".tmj", "map"}, {".tsj", "map"}, {".tmx", "map"}, {".tsx", "map"},
	{".html", "ui"}, {".css", "ui"}, {".frag", "shader"}, {".vert", "shader"}, {".glsl", "shader"}, {".md", "doc"},
}

func kindOf(rel string) string {
	switch path.Base(rel) {
	case ".jm.json":
		return "manifest"
	case "package.json", "package-lock.json", "tsconfig.json", "asconfig.json":
		return ""
	}
	if strings.HasPrefix(rel, goldenDir+"/") {
		return "golden" // its specs and their frames
	}
	for _, k := range fileKinds {
		if strings.HasSuffix(rel, k.suffix) {
			return k.kind
		}
	}
	return ""
}

// quoted finds the paths a text file names files with: in matching quotes
// (spaces and all), and in url(), inside another string too (a style attribute).
var quoted = regexp.MustCompile(`"([^"\n]*)"|'([^'\n]*)'|` + "`([^`\\n]*)`")
var urls = regexp.MustCompile(`url\(\s*(?:"([^"\n]*)"|'([^'\n]*)'|([^)"'\s]+))\s*\)`)

// codeOf says which bytes of a script are code: not in a comment or a string
// (a template's ${...} is code).
func codeOf(text []byte) []bool {
	code := make([]bool, len(text))
	var quote byte   // the string's quote, 0 outside one
	var braces []int // each open ${...}: the { inside it not closed yet
	for i := 0; i < len(text); i++ {
		c := text[i]
		switch {
		case quote == '`' && c == '$' && i+1 < len(text) && text[i+1] == '{':
			quote, braces = 0, append(braces, 0)
			i++
		case quote != 0:
			if c == '\\' {
				i++
			} else if c == quote {
				quote = 0
			}
		case c == '/' && i+1 < len(text) && text[i+1] == '/':
			for i < len(text) && text[i] != '\n' {
				i++
			}
		case c == '/' && i+1 < len(text) && text[i+1] == '*':
			end := bytes.Index(text[i+2:], []byte("*/"))
			if end < 0 {
				return code
			}
			i += end + 3
		case c == '"' || c == '\'' || c == '`':
			quote = c
		case c == '}' && len(braces) > 0 && braces[len(braces)-1] == 0: // the ${...} ends: the template goes on
			quote, braces = '`', braces[:len(braces)-1]
		default:
			if len(braces) > 0 && c == '{' {
				braces[len(braces)-1]++
			} else if len(braces) > 0 && c == '}' {
				braces[len(braces)-1]--
			}
			code[i] = true
		}
	}
	return code
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

// indexGame is every file of the game at root (build output, dot-folders and
// packages left out), sorted by kind and path.
func indexGame(root string) []indexEntry {
	byPath := map[string]*indexEntry{}
	_ = filepath.WalkDir(root, func(p string, d os.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		rel, _ := filepath.Rel(root, p)
		rel = filepath.ToSlash(rel)
		if d.IsDir() && rel != "." && hiddenFromAgents(rel) {
			return filepath.SkipDir
		}
		// Regular files only: a link may lead out of the game.
		if kind := kindOf(rel); d.Type().IsRegular() && kind != "" && !hiddenFromAgents(path.Dir(rel)) {
			byPath[rel] = &indexEntry{Path: rel, Kind: kind, Uses: []string{}, UsedBy: []string{}}
		}
		return nil
	})
	named := map[string][]string{} // scripts' resource calls name prefabs, scenes, shaders and sounds by file name
	for rel, e := range byPath {
		if slices.Contains([]string{"prefab", "scene", "shader", "audio"}, e.Kind) {
			stem := strings.TrimSuffix(strings.TrimSuffix(path.Base(rel), path.Ext(rel)), "."+e.Kind)
			named[stem] = append(named[stem], rel)
		}
	}
	// The files a name means: a path from the naming file's folder (maps, atlases
	// and UI name theirs so) or the game's, "atlas.json#region" its atlas, a
	// manifest's glob its matches, a script's file name the file.
	resolve := func(from *indexEntry, name string) []string {
		name, _, _ = strings.Cut(name, "#")
		if from.Kind == "manifest" && strings.ContainsAny(name, "*?") {
			matches, _ := manifest.ExpandAssets(os.DirFS(root), []string{name})
			return matches
		}
		here := path.Join(path.Dir(from.Path), name)
		// As each loads them: UI and atlases from the game's folder; Tiled maps and data (an Aseprite sheet) from their own.
		candidates := []string{name, here, here + ".ts"}
		switch from.Kind {
		case "ui", "atlas":
			candidates = []string{name, here}
		case "map", "data":
			candidates = []string{here, name}
		}
		for _, candidate := range candidates {
			if c := path.Clean(candidate); byPath[c] != nil && c != from.Path {
				return []string{c}
			}
		}
		return nil
	}
	link := func(from *indexEntry, target string) {
		if byPath[target] == nil || target == from.Path || slices.Contains(from.Uses, target) {
			return // only what was indexed (a glob also matches files the index doesn't know, and links)
		}
		from.Uses = append(from.Uses, target)
		byPath[target].UsedBy = append(byPath[target].UsedBy, from.Path)
	}
	for rel, e := range byPath {
		file := filepath.Join(root, filepath.FromSlash(rel))
		e.Detail = detailOf(e.Kind, file)
		var names []string
		text := textOf(e.Kind, file)
		switch {
		case text == nil:
		case strings.HasSuffix(rel, ".json") || strings.HasSuffix(rel, ".tmj") || strings.HasSuffix(rel, ".tsj"):
			var v any
			if json.Unmarshal(text, &v) == nil {
				names = jsonStrings(v, nil)
			}
			if e.Kind == "scene" {
				e.Detail = fmt.Sprintf("%d entities", countEntities(v))
			}
		default:
			for _, m := range quoted.FindAllStringSubmatch(string(text), -1) {
				names = append(names, m[1]+m[2]+m[3])
			}
			for _, m := range urls.FindAllStringSubmatch(string(text), -1) {
				names = append(names, m[1]+m[2]+m[3])
			}
		}
		if e.Kind == "script" {
			code := codeOf(text)
			for _, c := range resourceCalls {
				for at := 0; at < len(text); {
					m := c.call.FindSubmatchIndex(text[at:])
					if m == nil {
						break
					}
					start, arg := at+m[0], text[at+m[2]:at+m[3]]
					if !code[start] { // in a comment or a string: not a call, but one may start inside it
						at = start + 1
						continue
					}
					at += m[1]
					for _, q := range quoted.FindAllStringSubmatch(string(arg), -1) {
						for _, target := range named[q[1]+q[2]+q[3]] {
							if byPath[target].Kind == c.kind {
								link(e, target)
							}
						}
					}
				}
			}
		}
		for _, name := range names {
			for _, target := range resolve(e, name) {
				link(e, target)
			}
		}
	}
	// An image lights with the normal map beside it (x.png, x.normal.png): it uses it.
	for rel, e := range byPath {
		if e.Kind == "texture" && !atlas.IsNormalMap(rel) {
			link(e, atlas.NormalPath(rel))
		}
	}
	out := make([]indexEntry, 0, len(byPath))
	for _, e := range byPath {
		// Nothing names these: what the game starts from, and what tools read.
		e.Unused = len(e.UsedBy) == 0 && !slices.Contains([]string{"manifest", "test", "golden", "doc", "script"}, e.Kind)
		slices.Sort(e.Uses)
		slices.Sort(e.UsedBy)
		out = append(out, *e)
	}
	slices.SortFunc(out, func(a, b indexEntry) int {
		return strings.Compare(a.Kind+"\x00"+a.Path, b.Kind+"\x00"+b.Path)
	})
	return out
}

// jsonStrings are the string values in v (keys left out), appended to out.
func jsonStrings(v any, out []string) []string {
	switch v := v.(type) {
	case string:
		out = append(out, v)
	case []any:
		for _, item := range v {
			out = jsonStrings(item, out)
		}
	case map[string]any:
		for _, item := range v {
			out = jsonStrings(item, out)
		}
	}
	return out
}

// textOf is a file that names others (JSON, scripts, maps, UI), read; nil for the rest.
func textOf(kind, file string) []byte {
	if !slices.Contains([]string{"manifest", "scene", "prefab", "atlas", "data", "script", "test", "map", "ui"}, kind) {
		return nil
	}
	data, _ := os.ReadFile(file)
	return data
}

// detailOf says a little about a file: an image's size, a sound's (a scene's entities come from its text).
func detailOf(kind, file string) string {
	switch kind {
	case "texture":
		f, err := os.Open(file)
		if err != nil {
			return ""
		}
		defer f.Close()
		if c, _, err := image.DecodeConfig(f); err == nil { // the header only
			return fmt.Sprintf("%d×%d", c.Width, c.Height)
		}
	case "audio":
		if info, err := os.Stat(file); err == nil {
			return fmt.Sprintf("%d KB", (info.Size()+1023)/1024)
		}
	}
	return ""
}

// countEntities counts a scene document's entities and their children.
func countEntities(doc any) int {
	var count func(list []any) int
	count = func(list []any) int {
		n := 0
		for _, item := range list {
			n++
			if e, ok := item.(map[string]any); ok {
				children, _ := e["children"].([]any)
				n += count(children)
			}
		}
		return n
	}
	d, _ := doc.(map[string]any)
	entities, _ := d["entities"].([]any)
	return count(entities)
}
