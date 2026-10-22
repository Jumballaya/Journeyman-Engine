package main

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"

	"github.com/Jumballaya/Journeyman-Engine/internal/docs"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
)

// The open game's resources: the list, reading a jm:// or file URI, templates
// for what the list can't enumerate, completions and subscriptions (stdio only).

// resourceTemplates are the shapes of URI resources/read answers that
// resources/list can't enumerate (any game's files, every play, entity...).
func resourceTemplates() []map[string]any {
	t := func(uri, name, title, description, mime string) map[string]any {
		out := map[string]any{"uriTemplate": uri, "name": name, "title": title, "description": description}
		if mime != "" {
			out["mimeType"] = mime
		}
		return out
	}
	return []map[string]any{
		t(gameFileTemplate, "game-file", "A game's file", "a file of any of the person's games (a name or absolute path from games), from its folder", ""),
		t("jm://game/{game}/play/{play}", "play", "A recorded play", "what happened in a play of a game and its markers", "text/plain"),
		t("jm://game/{game}/play/{play}/{marker}", "marker", "A play's marker", `a marker ("m1") of a play: the moment, the note and what the person saw`, "text/plain"),
		t("jm://game/{game}/entity{?scene,name}", "entity", "A scene's entity", "every entity of that name in a game's scene, and where", "application/json"),
		t("jm://docs/{topic}", "docs", "Journeyman's docs", "a topic of jm docs", "text/markdown"),
	}
}

// path is simple expansion ("scenes%2Fmain.scene.json"): reserved expansion
// would keep a file named "a%20b" as it is, and it would read as "a b".
const gameFileTemplate = "jm://game/{game}/file/{path}"

// resourceWatch is what the client subscribed to and what it was when last
// seen, and the resources list as it last saw it.
type resourceWatch struct {
	mu     sync.Mutex
	stamps map[string]string // by uri
	listed bool              // the client has listed: it hears when the list changes
	list   string
}

// subscribe watches uri from now: an error when it isn't one of the game's.
func (s *mcpServer) subscribe(params json.RawMessage, on bool) (any, *rpcError) {
	if s.peer == nil {
		return nil, &rpcError{Code: -32601, Message: "subscriptions need stdio: HTTP can't tell the client"}
	}
	var p struct {
		URI string `json:"uri"`
	}
	if err := json.Unmarshal(params, &p); err != nil || p.URI == "" {
		return nil, &rpcError{Code: -32602, Message: "give the resource's uri"}
	}
	w := &s.watch
	if !on {
		w.mu.Lock()
		delete(w.stamps, p.URI)
		w.mu.Unlock()
		return map[string]any{}, nil
	}
	stamp, err := s.stampOf(p.URI)
	if err == nil {
		err = s.there(p.URI)
	}
	if err != nil {
		return nil, &rpcError{Code: -32002, Message: err.Error()}
	}
	w.mu.Lock()
	w.stamps[p.URI] = stamp
	w.mu.Unlock()
	return map[string]any{}, nil
}

// listedNow: the client has the list of the game at root as it is now.
func (w *resourceWatch) listedNow(root string) {
	stamp := listStamp(root)
	w.mu.Lock()
	w.listed, w.list = true, stamp
	w.mu.Unlock()
}

// pollResources tells the client of each subscribed resource that changed
// since it was last seen, and of a new list. It reads files only: no lock on
// the tool calls, so a long build doesn't hold the news back.
func (s *mcpServer) pollResources() {
	w := &s.watch
	w.mu.Lock()
	uris := make([]string, 0, len(w.stamps))
	for uri := range w.stamps {
		uris = append(uris, uri)
	}
	listed := w.listed
	w.mu.Unlock()

	now := map[string]string{}
	for _, uri := range uris {
		now[uri], _ = s.stampOf(uri) // gone: "", which changed too
	}
	list := ""
	if listed {
		list = listStamp(s.opened())
	}

	var changed []string
	w.mu.Lock()
	for uri, stamp := range now {
		if old, still := w.stamps[uri]; still && old != stamp {
			w.stamps[uri] = stamp
			changed = append(changed, uri)
		}
	}
	listChanged := false
	if listed { // else the list a client just took is kept, not this poll's nothing
		listChanged, w.list = list != w.list, list
	}
	w.mu.Unlock()

	sort.Strings(changed)
	for _, uri := range changed {
		data, _ := json.Marshal(map[string]any{"uri": uri})
		s.peer.send(rpcMessage{JSONRPC: "2.0", Method: "notifications/resources/updated", Params: data})
	}
	if listChanged {
		s.peer.send(rpcMessage{JSONRPC: "2.0", Method: "notifications/resources/list_changed"})
	}
}

// listStamp names the resources list of the game at root as it is (its files)
// and the plays and markers the templates reach.
func listStamp(root string) string {
	data, _ := json.Marshal(projectResources(root))
	all, _ := plays.List(root)
	for _, p := range all {
		data = fmt.Appendf(data, "\n%s %d", p.ID, len(p.Meta.Markers))
	}
	return fileEtag(data)
}

// fileStamp is a file's time and size: "" when it isn't there (yet), so its making is a change.
func fileStamp(file string) string {
	info, err := os.Stat(file)
	if err != nil {
		return ""
	}
	return fmt.Sprint(info.ModTime().UnixNano(), " ", info.Size())
}

// buildStamp is the game's build's newest time and file count, by a look at
// each file (plays.ReadBuild reads them all: too much every poll).
func buildStamp(root string) string {
	dir := filepath.Join(root, "build")
	var newest int64
	count := 0
	_ = filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() && filepath.Dir(path) == dir && (d.Name() == "logs" || d.Name() == "golden") {
			return filepath.SkipDir // runs write there, not builds (as ReadBuild skips them)
		}
		if info, err := d.Info(); err == nil && !d.IsDir() {
			newest, count = max(newest, info.ModTime().UnixNano()), count+1
		}
		return nil
	})
	return fmt.Sprint(newest, " ", count)
}

// completeArgument suggests values for a template's or prompt's argument
// starting with what's typed, as the open game has them.
func (s *mcpServer) completeArgument(params json.RawMessage) (any, *rpcError) {
	var p struct {
		Ref struct {
			Type, URI, Name string
		} `json:"ref"`
		Argument struct {
			Name  string  `json:"name"`
			Value *string `json:"value"`
		} `json:"argument"`
		Context struct {
			Arguments map[string]string `json:"arguments"`
		} `json:"context"`
	}
	if err := json.Unmarshal(params, &p); err != nil || p.Argument.Name == "" || p.Argument.Value == nil {
		return nil, &rpcError{Code: -32602, Message: "give ref and argument {name, value}"}
	}
	var values []string
	switch p.Ref.Type {
	case "ref/prompt":
		if _, ok := findPlaybook(p.Ref.Name); !ok {
			return nil, &rpcError{Code: -32602, Message: "unknown prompt " + p.Ref.Name}
		}
		s.calls.Lock()
		s.openRecentIfNone()
		root := s.opened()
		s.calls.Unlock()
		values = knowledgeValues(root, p.Argument.Name)
	case "ref/resource":
		known := false
		for _, t := range resourceTemplates() {
			known = known || t["uriTemplate"] == p.Ref.URI
		}
		if !known {
			return nil, &rpcError{Code: -32602, Message: "no resource template " + p.Ref.URI}
		}
		s.calls.Lock() // the open game's: not while open_game switches it
		s.openRecentIfNone()
		values = s.argumentValues(p.Argument.Name, p.Context.Arguments)
		s.calls.Unlock()
	default:
		return nil, &rpcError{Code: -32602, Message: "ref is ref/prompt or ref/resource"}
	}
	typed := strings.ToLower(*p.Argument.Value)
	matches, seen := []string{}, map[string]bool{}
	for _, v := range values {
		if strings.HasPrefix(strings.ToLower(v), typed) && !seen[v] {
			seen[v] = true
			matches = append(matches, v)
		}
	}
	const most = 100 // the protocol's limit
	return map[string]any{"completion": map[string]any{"values": matches[:min(len(matches), most)],
		"total": len(matches), "hasMore": len(matches) > most}}, nil
}

// argumentValues are what a template's argument can be: games, docs topics,
// and the parts of the game given (else the open one): files, plays, markers, scenes, entity names.
func (s *mcpServer) argumentValues(name string, given map[string]string) []string {
	switch name {
	case "game":
		return s.gameValues()
	case "topic":
		var out []string
		for _, t := range docs.Topics() {
			out = append(out, t.Name)
		}
		return out
	}
	root := s.opened()
	if game := given["game"]; game != "" {
		root, _ = findGame(s.home, game)
	}
	if root == "" { // no game, or one there isn't: nothing of one
		return nil
	}
	var out []string
	switch name {
	case "path":
		out = filesOf(root, "")
	case "play":
		all, _ := plays.List(root)
		for _, p := range all {
			out = append(out, p.ID)
		}
	case "marker":
		if p, err := plays.Find(root, given["play"]); err == nil {
			for _, m := range p.Meta.Markers {
				out = append(out, fmt.Sprintf("m%d", m.N))
			}
		}
	case "scene":
		out = filesOf(root, ".scene.json")
	case "name":
		scenes := filesOf(root, ".scene.json")
		if scene := given["scene"]; scene != "" {
			scenes = []string{scene}
		}
		for _, scene := range scenes {
			if real, _, err := gamePath(root, scene); err == nil {
				eachEntity(readScene(real).Entities, "entities", func(e map[string]any, _ string) {
					if n, _ := e["name"].(string); n != "" {
						out = append(out, n)
					}
				})
			}
		}
	}
	return out
}

// gameValues are the games a URI can name: the person's, by name when in the
// games folder (as findGame takes them), and the open game though it isn't listed.
func (s *mcpServer) gameValues() []string {
	open, listed := s.opened(), false
	var out []string
	for _, p := range projects.List(s.home) {
		if p.Missing {
			continue
		}
		listed = listed || evalOrSelf(p.Path) == evalOrSelf(open)
		if filepath.Dir(p.Path) == evalOrSelf(s.home) {
			out = append(out, filepath.Base(p.Path))
		} else {
			out = append(out, p.Path)
		}
	}
	if open != "" && !listed {
		out = append(out, open) // from a root, say
	}
	return out
}

// filesOf is the game at root's files ending in suffix, from its folder.
func filesOf(root, suffix string) []string {
	var out []string
	_ = walkProject(root, func(rel string, _ fs.FileInfo) {
		if strings.HasSuffix(rel, suffix) {
			out = append(out, rel)
		}
	})
	return out
}

// projectResources lists the files an agent reads and writes, and the schema.
func projectResources(root string) []map[string]any {
	resources := []map[string]any{{"uri": gameURI(root, "jm://schema"), "name": "schema", "mimeType": "application/json",
		"description": "every component's scene keys and script fields (jm schema)"},
		{"uri": gameURI(root, "jm://memory"), "name": "memory", "mimeType": "application/json", "description": "the game's design notes (design/): each one's title, observations and links"}}
	for _, t := range docs.Topics() {
		resources = append(resources, map[string]any{"uri": "jm://docs/" + t.Name, "name": "docs/" + t.Name,
			"mimeType": "text/markdown", "description": t.Title})
	}
	// Only what resources/read gives back.
	for _, f := range gameFiles(root, func(rel string) bool { return mimeOf(rel) != "" }) {
		resources = append(resources, map[string]any{"uri": fileURI(filepath.Join(root, f)), "name": f, "mimeType": mimeOf(f)})
	}
	return resources
}

// fileURI is the file:// URI of an absolute path, each segment escaped
// ("a b#2.ts": "a%20b%232.ts"); readResource unescapes it.
func fileURI(abs string) string {
	segments := strings.Split(filepath.ToSlash(abs), "/")
	for i, s := range segments {
		segments[i] = url.PathEscape(s)
	}
	return "file://" + strings.Join(segments, "/")
}

func mimeOf(path string) string {
	switch {
	case strings.HasSuffix(path, ".json"), strings.HasSuffix(path, ".tmj"), strings.HasSuffix(path, ".tsj"):
		return "application/json"
	case strings.HasSuffix(path, ".ts"):
		return "text/x-typescript"
	case strings.HasSuffix(path, ".html"):
		return "text/html"
	case strings.HasSuffix(path, ".css"):
		return "text/css"
	case strings.HasSuffix(path, ".frag"), strings.HasSuffix(path, ".txt"), strings.HasSuffix(path, ".md"):
		return "text/plain"
	}
	return ""
}
