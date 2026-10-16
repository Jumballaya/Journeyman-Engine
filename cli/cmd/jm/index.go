package main

import (
	"encoding/json"
	"fmt"
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

// quoted finds the strings a text file names files with: quoted, or in url().
var quoted = regexp.MustCompile(`["'\x60]([^"'\x60\s]+)["'\x60]|url\(\s*([^)"'\s]+)\s*\)`)

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
		if kind := kindOf(rel); !d.IsDir() && kind != "" && !hiddenFromAgents(path.Dir(rel)) {
			byPath[rel] = &indexEntry{Path: rel, Kind: kind, Uses: []string{}, UsedBy: []string{}}
		}
		return nil
	})
	sounds := map[string][]string{} // scripts play sounds by name: Audio.play("hit")
	for rel, e := range byPath {
		if e.Kind == "audio" {
			stem := strings.TrimSuffix(path.Base(rel), path.Ext(rel))
			sounds[stem] = append(sounds[stem], rel)
		}
	}
	// The files a name means: a path from the game's folder or the naming file's
	// (a map's image), "atlas.json#region" its atlas, a manifest's glob its matches.
	resolve := func(from, name string) []string {
		name, _, _ = strings.Cut(name, "#")
		if strings.Contains(name, "*") {
			var matches []string
			for rel := range byPath {
				if ok, _ := path.Match(name, rel); ok {
					matches = append(matches, rel)
				}
			}
			return matches
		}
		for _, candidate := range []string{name, path.Join(path.Dir(from), name), path.Join(path.Dir(from), name) + ".ts"} {
			if c := path.Clean(candidate); byPath[c] != nil && c != from {
				return []string{c}
			}
		}
		if k := byPath[from].Kind; k == "script" || k == "ui" {
			return sounds[name]
		}
		return nil
	}
	for rel, e := range byPath {
		data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(rel)))
		if err != nil {
			continue
		}
		used := map[string]bool{}
		var names []string
		var v any
		if strings.HasSuffix(rel, ".json") || strings.HasSuffix(rel, ".tmj") || strings.HasSuffix(rel, ".tsj") {
			if json.Unmarshal(data, &v) == nil {
				names = jsonStrings(v, nil)
			}
		} else if e.Kind == "script" || e.Kind == "test" || e.Kind == "ui" || e.Kind == "map" {
			for _, m := range quoted.FindAllStringSubmatch(string(data), -1) {
				names = append(names, m[1]+m[2])
			}
		}
		for _, name := range names {
			for _, target := range resolve(rel, name) {
				if !used[target] {
					used[target] = true
					e.Uses = append(e.Uses, target)
					byPath[target].UsedBy = append(byPath[target].UsedBy, rel)
				}
			}
		}
		e.Detail = detailOf(e.Kind, data, v)
	}
	out := make([]indexEntry, 0, len(byPath))
	for _, e := range byPath {
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

// detailOf says a little about a file: a scene's entities, an image's size.
func detailOf(kind string, data []byte, parsed any) string {
	switch kind {
	case "scene":
		doc, _ := parsed.(map[string]any)
		entities, _ := doc["entities"].([]any)
		n := 0
		var count func(list []any)
		count = func(list []any) {
			for _, item := range list {
				n++
				if e, ok := item.(map[string]any); ok {
					children, _ := e["children"].([]any)
					count(children)
				}
			}
		}
		count(entities)
		return fmt.Sprintf("%d entities", n)
	case "texture":
		if c, _, err := image.DecodeConfig(strings.NewReader(string(data))); err == nil {
			return fmt.Sprintf("%d×%d", c.Width, c.Height)
		}
	case "audio":
		return fmt.Sprintf("%d KB", (len(data)+1023)/1024)
	}
	return ""
}
