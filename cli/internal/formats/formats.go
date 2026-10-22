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
	Suffix     string
	Kind       string   // what it is in the game (game_index): scene, script, texture, data, …
	Archive    string   // the entry type the engine loads it as, packed; "" isn't shipped
	Pick       string   // the pick tool's kind offering it; "" isn't offered
	Upload     bool     // asset_add takes it
	UploadMIME []string // content types a host may serve an upload as (a generic binary is always taken)
	TextMIME   string   // the content type jm serves it as text (resources, file views); "" isn't text
	Plays      bool     // a change to it can change how the game plays (scripts, data, UI layout), not only how it looks or sounds
}

// jsonMIME: how hosts serve a JSON file.
var jsonMIME = []string{"application/json", "text/plain"}

const json, ts, plain = "application/json", "text/x-typescript", "text/plain"

// table: a source format (Aseprite, Tiled's XML, a bare shader stage) has no
// Archive: the build bakes it or the engine doesn't read it.
var table = []Format{
	{Suffix: ".jm.json", Kind: "manifest", Archive: "manifest", TextMIME: json, Plays: true},
	{Suffix: ".scene.json", Kind: "scene", Archive: "scene", Pick: "scene", Upload: true, UploadMIME: jsonMIME, TextMIME: json, Plays: true},
	{Suffix: ".prefab.json", Kind: "prefab", Archive: "prefab", Pick: "prefab", Upload: true, UploadMIME: jsonMIME, TextMIME: json, Plays: true},
	{Suffix: ".atlas.json", Kind: "atlas", Archive: "atlas", Upload: true, UploadMIME: jsonMIME, TextMIME: json, Plays: true},
	{Suffix: ".bindings.json", Kind: "data", Archive: "bindings", Upload: true, UploadMIME: jsonMIME, TextMIME: json, Plays: true},
	{Suffix: ".golden.json", Kind: "golden", TextMIME: json, Plays: true},
	{Suffix: ".json", Kind: "data", Archive: "data", Upload: true, UploadMIME: jsonMIME, TextMIME: json, Plays: true}, // read by scripts as is
	{Suffix: ".spec.ts", Kind: "test", TextMIME: ts, Plays: true},
	{Suffix: ".d.ts", TextMIME: ts, Plays: true},
	{Suffix: ".ts", Kind: "script", Archive: "script", Pick: "script", TextMIME: ts, Plays: true}, // jm build leaves its wasm at the .ts path
	{Suffix: ".png", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, UploadMIME: []string{"image/png"}},
	{Suffix: ".jpg", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, UploadMIME: []string{"image/jpeg"}},
	{Suffix: ".jpeg", Kind: "texture", Archive: "image", Pick: "texture", Upload: true, UploadMIME: []string{"image/jpeg"}},
	{Suffix: ".aseprite", Kind: "texture", Upload: true},
	{Suffix: ".ase", Kind: "texture", Upload: true},
	{Suffix: ".wav", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, UploadMIME: []string{"audio/wav", "audio/x-wav", "audio/wave", "audio/vnd.wave"}},
	{Suffix: ".ogg", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, UploadMIME: []string{"audio/ogg", "application/ogg"}},
	{Suffix: ".mp3", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, UploadMIME: []string{"audio/mpeg"}},
	{Suffix: ".flac", Kind: "audio", Archive: "audio", Pick: "sound", Upload: true, UploadMIME: []string{"audio/flac", "audio/x-flac"}},
	{Suffix: ".ttf", Kind: "font", Archive: "font", Upload: true, UploadMIME: []string{"font/ttf", "application/x-font-ttf"}, Plays: true},
	{Suffix: ".otf", Kind: "font", Archive: "font", Upload: true, UploadMIME: []string{"font/otf", "application/x-font-otf"}, Plays: true},
	{Suffix: ".fnt", Kind: "font", TextMIME: plain, Plays: true},
	{Suffix: ".tmj", Kind: "map", Archive: "tilemap", Pick: "map", Upload: true, UploadMIME: jsonMIME, TextMIME: json, Plays: true},
	{Suffix: ".tsj", Kind: "map", Archive: "tileset", Upload: true, UploadMIME: jsonMIME, TextMIME: json, Plays: true},
	{Suffix: ".tmx", Kind: "map", TextMIME: "application/xml"},
	{Suffix: ".tsx", Kind: "map", TextMIME: "application/xml"},
	{Suffix: ".ui.html", Kind: "ui", Archive: "ui", Pick: "ui", TextMIME: "text/html", Plays: true},
	{Suffix: ".html", Kind: "ui", TextMIME: "text/html", Plays: true},
	{Suffix: ".css", Kind: "ui", Archive: "stylesheet", TextMIME: "text/css", Plays: true},
	{Suffix: ".frag", Kind: "shader", Archive: "shader", TextMIME: plain},
	{Suffix: ".vert", Kind: "shader", TextMIME: plain},
	{Suffix: ".glsl", Kind: "shader", TextMIME: plain},
	{Suffix: ".md", Kind: "doc", TextMIME: "text/markdown"},
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
