// Package formats is every kind of file a game has, in one table: what it is
// to agents and the editor, what the engine loads it as once packed, and
// which tools offer it. A format added here is indexed, shipped, pickable
// and uploadable as its row says, everywhere.
package formats

import (
	"cmp"
	"slices"
	"strings"
)

// A Format is the files ending in Suffix.
type Format struct {
	Suffix  string
	Kind    string   // what it is in the game (game_index): scene, script, texture, data, …
	Archive string   // the entry type the engine loads it as, packed; "" isn't shipped
	Pick    string   // the pick tool's kind offering it; "" isn't offered
	Upload  bool     // asset_add takes it
	MIME    []string // content types a host may serve an upload as (a generic binary is always taken)
	Look    bool     // a change to it changes how the game looks or sounds, never how it plays
}

// table: a source format (Aseprite, Tiled's XML, a bare shader stage) has no
// Archive: the build bakes it or the engine doesn't read it.
var table = []Format{
	{Suffix: ".jm.json", Kind: "manifest", Archive: "manifest"},
	{Suffix: ".scene.json", Kind: "scene", Archive: "scene", Pick: "scene"},
	{Suffix: ".prefab.json", Kind: "prefab", Archive: "prefab", Pick: "prefab"},
	{Suffix: ".atlas.json", Kind: "atlas", Archive: "atlas"},
	{Suffix: ".bindings.json", Kind: "data", Archive: "bindings"},
	{Suffix: ".golden.json", Kind: "golden"},
	{Suffix: ".json", Kind: "data", Archive: "data", Upload: true, MIME: []string{"application/json", "text/plain"}}, // read by scripts as is
	{Suffix: ".spec.ts", Kind: "test"},
	{Suffix: ".d.ts"},
	{Suffix: ".ts", Kind: "script", Archive: "script", Pick: "script"}, // jm build leaves its wasm at the .ts path
	{Suffix: ".png", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, MIME: []string{"image/png"}, Look: true},
	{Suffix: ".jpg", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, MIME: []string{"image/jpeg"}, Look: true},
	{Suffix: ".jpeg", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, MIME: []string{"image/jpeg"}, Look: true},
	{Suffix: ".aseprite", Kind: "texture", Upload: true, Look: true},
	{Suffix: ".ase", Kind: "texture", Upload: true, Look: true},
	{Suffix: ".wav", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, MIME: []string{"audio/wav", "audio/x-wav", "audio/wave", "audio/vnd.wave"}, Look: true},
	{Suffix: ".ogg", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, MIME: []string{"audio/ogg", "application/ogg"}, Look: true},
	{Suffix: ".mp3", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, MIME: []string{"audio/mpeg"}, Look: true},
	{Suffix: ".flac", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, MIME: []string{"audio/flac", "audio/x-flac"}, Look: true},
	{Suffix: ".ttf", Kind: "font", Archive: "font", Upload: true, MIME: []string{"font/ttf", "application/x-font-ttf"}},
	{Suffix: ".otf", Kind: "font", Archive: "font", Upload: true, MIME: []string{"font/otf", "application/x-font-otf"}},
	{Suffix: ".fnt", Kind: "font"},
	{Suffix: ".tmj", Kind: "map", Archive: "tilemap", Pick: "map", Upload: true, MIME: []string{"application/json", "text/plain"}},
	{Suffix: ".tsj", Kind: "map", Archive: "tileset", Upload: true, MIME: []string{"application/json", "text/plain"}},
	{Suffix: ".tmx", Kind: "map"},
	{Suffix: ".tsx", Kind: "map"},
	{Suffix: ".ui.html", Kind: "ui", Archive: "ui", Pick: "ui"},
	{Suffix: ".html", Kind: "ui"},
	{Suffix: ".css", Kind: "ui", Archive: "stylesheet"},
	{Suffix: ".frag", Kind: "shader", Archive: "shader", Look: true},
	{Suffix: ".vert", Kind: "shader", Look: true},
	{Suffix: ".glsl", Kind: "shader", Look: true},
	{Suffix: ".md", Kind: "doc"},
}

func init() { // longest suffix first: .scene.json before .json
	slices.SortStableFunc(table, func(a, b Format) int { return cmp.Compare(len(b.Suffix), len(a.Suffix)) })
}

// Of is the format of the file name (or path) names, in any case ("hero.PNG"
// is a .png); false for none.
func Of(name string) (Format, bool) {
	name = strings.ToLower(name)
	i := slices.IndexFunc(table, func(f Format) bool { return strings.HasSuffix(name, f.Suffix) })
	if i < 0 {
		return Format{}, false
	}
	return table[i], true
}

// All are the formats whose row keep says to, longest suffix first.
func All(keep func(Format) bool) []Format {
	return slices.DeleteFunc(slices.Clone(table), func(f Format) bool { return !keep(f) })
}
