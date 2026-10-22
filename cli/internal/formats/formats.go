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
	Plays   bool     // a change to it can change how the game plays (scripts, data, UI layout), not only how it looks or sounds
}

// jsonMIME: how hosts serve a JSON file.
var jsonMIME = []string{"application/json", "text/plain"}

// table: a source format (Aseprite, Tiled's XML, a bare shader stage) has no
// Archive: the build bakes it or the engine doesn't read it.
var table = []Format{
	{Suffix: ".jm.json", Kind: "manifest", Archive: "manifest", Plays: true},
	{Suffix: ".scene.json", Kind: "scene", Archive: "scene", Pick: "scene", Upload: true, MIME: jsonMIME, Plays: true},
	{Suffix: ".prefab.json", Kind: "prefab", Archive: "prefab", Pick: "prefab", Upload: true, MIME: jsonMIME, Plays: true},
	{Suffix: ".atlas.json", Kind: "atlas", Archive: "atlas", Upload: true, MIME: jsonMIME, Plays: true},
	{Suffix: ".bindings.json", Kind: "data", Archive: "bindings", Upload: true, MIME: jsonMIME, Plays: true},
	{Suffix: ".golden.json", Kind: "golden", Plays: true},
	{Suffix: ".json", Kind: "data", Archive: "data", Upload: true, MIME: jsonMIME, Plays: true}, // read by scripts as is
	{Suffix: ".spec.ts", Kind: "test", Plays: true},
	{Suffix: ".d.ts", Plays: true},
	{Suffix: ".ts", Kind: "script", Archive: "script", Pick: "script", Plays: true}, // jm build leaves its wasm at the .ts path
	{Suffix: ".png", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, MIME: []string{"image/png"}},
	{Suffix: ".jpg", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, MIME: []string{"image/jpeg"}},
	{Suffix: ".jpeg", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, MIME: []string{"image/jpeg"}},
	{Suffix: ".aseprite", Kind: "texture", Upload: true},
	{Suffix: ".ase", Kind: "texture", Upload: true},
	{Suffix: ".wav", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, MIME: []string{"audio/wav", "audio/x-wav", "audio/wave", "audio/vnd.wave"}},
	{Suffix: ".ogg", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, MIME: []string{"audio/ogg", "application/ogg"}},
	{Suffix: ".mp3", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, MIME: []string{"audio/mpeg"}},
	{Suffix: ".flac", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, MIME: []string{"audio/flac", "audio/x-flac"}},
	{Suffix: ".ttf", Kind: "font", Archive: "font", Upload: true, MIME: []string{"font/ttf", "application/x-font-ttf"}, Plays: true},
	{Suffix: ".otf", Kind: "font", Archive: "font", Upload: true, MIME: []string{"font/otf", "application/x-font-otf"}, Plays: true},
	{Suffix: ".fnt", Kind: "font", Plays: true},
	{Suffix: ".tmj", Kind: "map", Archive: "tilemap", Pick: "map", Upload: true, MIME: jsonMIME, Plays: true},
	{Suffix: ".tsj", Kind: "map", Archive: "tileset", Upload: true, MIME: jsonMIME, Plays: true},
	{Suffix: ".tmx", Kind: "map"},
	{Suffix: ".tsx", Kind: "map"},
	{Suffix: ".ui.html", Kind: "ui", Archive: "ui", Pick: "ui", Plays: true},
	{Suffix: ".html", Kind: "ui", Plays: true},
	{Suffix: ".css", Kind: "ui", Archive: "stylesheet", Plays: true},
	{Suffix: ".frag", Kind: "shader", Archive: "shader"},
	{Suffix: ".vert", Kind: "shader"},
	{Suffix: ".glsl", Kind: "shader"},
	{Suffix: ".md", Kind: "doc"},
}

func init() { // longest suffix first: .scene.json before .json
	slices.SortStableFunc(table, func(a, b Format) int { return cmp.Compare(len(b.Suffix), len(a.Suffix)) })
}

// Of is the format of the file name (or path) names, false for none. Its
// case counts (jm build's does): lower it first to ask what it's meant as.
func Of(name string) (Format, bool) {
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
