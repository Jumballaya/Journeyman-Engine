package main

import (
	"cmp"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"mime"
	"net/url"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/docs"
	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
)

// What a resource URI names. A link is a part of a game as its files write it
// (tasks, notes): jm://file/{path}, jm://play/{play}[/{marker}], jm://entity?scene=…&name=…,
// jm://memory, jm://schema. jm://game/{game}/ before a link's tail names the
// game too, as a file:// URI does: the server hands out only those, so what a
// host holds reads the same after open_game. A bare link is the open game's.

// gameURI is link as a URI of the game at root ("" root, or a file:// URI: as it is).
func gameURI(root, link string) string {
	tail, ok := strings.CutPrefix(link, "jm://")
	if root == "" || !ok {
		return link
	}
	return "jm://game/" + url.PathEscape(root) + "/" + tail
}

func fileLink(rel string) string { return "jm://file/" + url.PathEscape(filepath.ToSlash(rel)) }

func playLink(id string, marker int) string {
	if marker == 0 {
		return "jm://play/" + url.PathEscape(id)
	}
	return fmt.Sprintf("jm://play/%s/m%d", url.PathEscape(id), marker)
}

func entityLink(scene, name string) string {
	return "jm://entity?" + url.Values{"scene": {scene}, "name": {name}}.Encode()
}

// gameLink is a link taken apart: one kind, and what that kind names.
type gameLink struct {
	kind         string // file, play, entity, memory or schema
	path         string // a file's; an entity's scene
	play, marker string // marker: "m1", "" for the play itself
	name         string // an entity's
}

// resolve is the game uri is of and what it names there: jm://game/'s game,
// the game a file:// URI's file is in, else the open game. Only schema needs none.
func (s *mcpServer) resolve(uri string) (string, gameLink, error) {
	root, link := s.opened(), uri
	if rest, ok := strings.CutPrefix(uri, "jm://game/"); ok {
		game, tail, found := strings.Cut(rest, "/")
		if !found {
			return "", gameLink{}, fmt.Errorf("a game's resource is jm://game/{game}/… (games names them)")
		}
		game, err := url.PathUnescape(game)
		if err == nil {
			root, err = findGame(s.home, game)
		}
		if err != nil {
			return "", gameLink{}, err
		}
		link = "jm://" + tail
	} else if path, ok := strings.CutPrefix(uri, "file://"); ok {
		path, err := url.PathUnescape(path)
		if err != nil {
			return "", gameLink{}, err
		}
		if root = gameHolding(path); root == "" {
			return "", gameLink{}, fmt.Errorf("%s isn't in a game", path)
		}
		return root, gameLink{kind: "file", path: path}, nil
	}
	l, err := parseLink(link)
	if err == nil && root == "" && l.kind != "schema" { // schema: the engine's components, without a game's scripts
		err = errNoGame
	}
	return root, l, err
}

func parseLink(link string) (gameLink, error) {
	rest, _ := strings.CutPrefix(link, "jm://")
	kind, tail, _ := strings.Cut(rest, "/")
	switch {
	case kind == "file":
		path, err := url.PathUnescape(tail)
		return gameLink{kind: "file", path: path}, err
	case kind == "play" && tail != "":
		id, marker, _ := strings.Cut(tail, "/")
		id, err := url.PathUnescape(id)
		if err != nil || id == "." || id == ".." || strings.ContainsAny(id, `/\`) {
			return gameLink{}, fmt.Errorf("no play %s", id)
		}
		return gameLink{kind: "play", play: id, marker: marker}, nil
	case strings.HasPrefix(rest, "entity?"):
		q, err := url.ParseQuery(strings.TrimPrefix(rest, "entity?"))
		return gameLink{kind: "entity", path: q.Get("scene"), name: q.Get("name")}, err
	case rest == "memory", rest == "schema":
		return gameLink{kind: rest}, nil
	}
	return gameLink{}, fmt.Errorf("unknown resource %s", link)
}

// readURI is what resources/read gives for uri: a widget, a docs topic, or a part of a game.
func (s *mcpServer) readURI(uri string) ([]map[string]any, error) {
	if content, ok := readWidget(uri); ok {
		return []map[string]any{content}, nil
	}
	if topic, ok := strings.CutPrefix(uri, "jm://docs/"); ok {
		text, err := docs.Read(topic)
		return []map[string]any{{"uri": uri, "mimeType": "text/markdown", "text": text}}, err
	}
	root, l, err := s.resolve(uri)
	if err != nil {
		return nil, err
	}
	switch l.kind {
	case "file":
		return readFileResource(root, uri, l.path)
	case "play":
		return readPlayResource(root, uri, l)
	case "entity":
		return readEntityResource(root, uri, l)
	case "memory":
		data, err := json.Marshal(map[string]any{"notes": memory.Notes(root)})
		return []map[string]any{{"uri": uri, "mimeType": "application/json", "text": string(data)}}, err
	}
	out, failed := runJM(nil, root, "schema")
	if failed {
		return nil, fmt.Errorf("%s", out)
	}
	return []map[string]any{{"uri": uri, "mimeType": "application/json", "text": out}}, nil
}

func readFileResource(root, uri, path string) ([]map[string]any, error) {
	f, err := readGameFile(root, path)
	if err != nil {
		return nil, err
	}
	if !f.Binary {
		return []map[string]any{{"uri": uri, "mimeType": cmp.Or(mimeOf(f.Path), "text/plain"), "text": f.Text}}, nil
	}
	if f.Size > textLimit {
		return nil, fmt.Errorf("%s is %s: too big to read here", f.Path, sizeText(int64(f.Size)))
	}
	data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(f.Path)))
	if err != nil {
		return nil, err
	}
	kind := cmp.Or(mime.TypeByExtension(filepath.Ext(f.Path)), "application/octet-stream")
	return []map[string]any{{"uri": uri, "mimeType": kind, "blob": base64.StdEncoding.EncodeToString(data)}}, nil
}

func readPlayResource(root, uri string, l gameLink) ([]map[string]any, error) {
	p, b, err := openPlay(root, l.play)
	if err != nil {
		return nil, err
	}
	if p.ID != l.play {
		return nil, fmt.Errorf("no play %s", l.play) // a prefix finds a play: a link names one
	}
	if l.marker == "" {
		o, err := overviewOf(root, p, b)
		if err != nil {
			return nil, err
		}
		return []map[string]any{{"uri": uri, "mimeType": "text/plain", "text": o.text()}}, nil
	}
	m, err := markerOf(p, l.marker)
	if err != nil {
		return nil, err
	}
	contents := []map[string]any{{"uri": uri, "mimeType": "text/plain", "text": momentSummary(p, m)}}
	if data := jpegImage(momentPicture(p, m), 960); data != nil {
		contents = append(contents, map[string]any{"uri": uri, "mimeType": "image/jpeg", "blob": base64.StdEncoding.EncodeToString(data)})
	}
	return contents, nil
}

func readEntityResource(root, uri string, l gameLink) ([]map[string]any, error) {
	real, rel, err := gamePath(root, l.path)
	if err != nil {
		return nil, err
	}
	if !strings.HasSuffix(rel, ".scene.json") {
		return nil, fmt.Errorf("%s isn't one of the project's scenes", l.path)
	}
	type found struct {
		Where  string         `json:"where"`
		Entity map[string]any `json:"entity"`
	}
	var matches []found
	eachEntity(readScene(real).Entities, "entities", func(e map[string]any, where string) {
		if e["name"] == l.name {
			matches = append(matches, found{where, e})
		}
	})
	if len(matches) == 0 {
		return nil, fmt.Errorf("no entity %q in %s", l.name, l.path)
	}
	data, _ := json.MarshalIndent(map[string]any{"scene": filepath.ToSlash(rel), "name": l.name, "entities": matches}, "", "  ")
	return []map[string]any{{"uri": uri, "mimeType": "application/json", "text": string(data)}}, nil
}

// stampOf names the version of what uri reads: the time and size of the file
// it comes from. What doesn't change while jm runs (schema, docs, widgets) is "-".
func (s *mcpServer) stampOf(uri string) (string, error) {
	if _, ok := readWidget(uri); ok || strings.HasPrefix(uri, "jm://docs/") {
		return "-", nil
	}
	root, l, err := s.resolve(uri)
	if err != nil {
		return "", err
	}
	switch l.kind {
	case "schema":
		return "-", nil
	case "memory":
		data, _ := json.Marshal(memory.Notes(root))
		return fileEtag(data), nil
	case "play": // its markers are in session.json; whether its build is still the build, in build/
		return fileStamp(filepath.Join(plays.Root(root), l.play, "session.json")) + " " + buildStamp(root), nil
	}
	file, _, err := gamePath(root, l.path) // a file, an entity's scene
	if err != nil {
		return "", err
	}
	return fileStamp(file), nil
}

// there: a doc, play or marker uri names one there is, in a game there is.
// Checked once, when subscribed: polls only stat. A file needn't be there yet.
func (s *mcpServer) there(uri string) error {
	if _, ok := readWidget(uri); ok {
		return nil
	}
	if topic, ok := strings.CutPrefix(uri, "jm://docs/"); ok {
		_, err := docs.Read(topic)
		return err
	}
	root, l, err := s.resolve(uri)
	if err != nil || l.kind != "play" {
		return err
	}
	p, err := plays.Load(filepath.Join(plays.Root(root), l.play))
	if err != nil {
		return fmt.Errorf("no play %s", l.play)
	}
	if l.marker == "" {
		return nil
	}
	_, err = markerOf(p, l.marker)
	return err
}
