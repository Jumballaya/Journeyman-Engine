package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io/fs"
	"mime"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"

	"github.com/Jumballaya/Journeyman-Engine/internal/docs"
	"github.com/Jumballaya/Journeyman-Engine/internal/memory"
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
		t("jm://play/{play}", "play", "A recorded play", "what happened in a play and its markers", "text/plain"),
		t("jm://play/{play}/{marker}", "marker", "A play's marker", `a marker ("m1") of a play: the moment, the note and what the person saw`, "text/plain"),
		t("jm://entity{?scene,name}", "entity", "A scene's entity", "every entity of that name in the open game's scene, and where", "application/json"),
		t("jm://docs/{topic}", "docs", "Journeyman's docs", "a topic of jm docs", "text/markdown"),
	}
}

// path is simple expansion ("scenes%2Fmain.scene.json"): reserved expansion
// would keep a file named "a%20b" as it is, and it would read as "a b".
const gameFileTemplate = "jm://game/{game}/file/{path}"

// readGameResource reads a jm://game/{game}/file/{path}; false: uri isn't one.
func (s *mcpServer) readGameResource(uri string) ([]map[string]any, bool, error) {
	rest, ok := strings.CutPrefix(uri, "jm://game/")
	if !ok {
		return nil, false, nil
	}
	root, path, err := s.gameFileOf(rest)
	if err != nil {
		return nil, true, err
	}
	f, err := readGameFile(root, path)
	if err != nil {
		return nil, true, err
	}
	if !f.Binary {
		kind := mimeOf(f.Path)
		if kind == "" {
			kind = "text/plain"
		}
		return []map[string]any{{"uri": uri, "mimeType": kind, "text": f.Text}}, true, nil
	}
	if f.Size > textLimit {
		return nil, true, fmt.Errorf("%s is %s: too big to read here", f.Path, sizeText(int64(f.Size)))
	}
	data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(f.Path)))
	if err != nil {
		return nil, true, err
	}
	kind := mime.TypeByExtension(filepath.Ext(f.Path))
	if kind == "" {
		kind = "application/octet-stream"
	}
	return []map[string]any{{"uri": uri, "mimeType": kind, "blob": base64.StdEncoding.EncodeToString(data)}}, true, nil
}

// gameFileOf is the game and path a game file URI (past jm://game/) names:
// any game open_game would open, and a path gamePath accepts there.
func (s *mcpServer) gameFileOf(rest string) (root, path string, err error) {
	game, path, found := strings.Cut(rest, "/file/")
	if !found {
		return "", "", fmt.Errorf("a game's file is %s", gameFileTemplate)
	}
	if game, err = url.PathUnescape(game); err == nil {
		path, err = url.PathUnescape(path)
	}
	if err != nil {
		return "", "", err
	}
	if root, err = findGame(s.home, game); err != nil {
		return "", "", err
	}
	return root, path, nil
}

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
	root := s.opened()
	stamp, err := s.stampOf(root, p.URI)
	if err == nil {
		err = there(root, p.URI)
	}
	if err != nil {
		return nil, &rpcError{Code: -32002, Message: err.Error()}
	}
	w.mu.Lock()
	w.stamps[p.URI] = stamp
	w.mu.Unlock()
	return map[string]any{}, nil
}

// there: a doc, play or marker uri names one there is. Checked once, when
// subscribed: polls only stat. A file needn't be there yet.
func there(root, uri string) error {
	if topic, ok := strings.CutPrefix(uri, "jm://docs/"); ok {
		_, err := docs.Read(topic)
		return err
	}
	rest, ok := strings.CutPrefix(uri, "jm://play/")
	if !ok {
		return nil
	}
	id, marker, _ := strings.Cut(rest, "/")
	id, _ = url.PathUnescape(id) // stampOf took it
	p, err := plays.Load(filepath.Join(plays.Root(root), id))
	if err != nil {
		return fmt.Errorf("no play %s", id)
	}
	if marker == "" {
		return nil
	}
	for _, m := range p.Meta.Markers {
		if fmt.Sprintf("m%d", m.N) == marker {
			return nil
		}
	}
	return fmt.Errorf("play %s has no marker %s", id, marker)
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

	root := s.opened()
	now := map[string]string{}
	for _, uri := range uris {
		now[uri], _ = s.stampOf(root, uri) // gone: "", which changed too
	}
	list := ""
	if listed {
		list = listStamp(root)
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

// stampOf names the version of what uri reads, in the open game at root:
// the file it comes from's time and size. Resources that don't change while
// jm runs (schema, docs, widgets) are "-".
func (s *mcpServer) stampOf(root, uri string) (string, error) {
	file := ""
	switch {
	case strings.HasPrefix(uri, "file://"):
		path, err := url.PathUnescape(strings.TrimPrefix(uri, "file://"))
		if err != nil {
			return "", err
		}
		if file, _, err = gamePath(root, path); err != nil {
			return "", err
		}
	case strings.HasPrefix(uri, "jm://game/"):
		game, path, err := s.gameFileOf(strings.TrimPrefix(uri, "jm://game/"))
		if err == nil {
			file, _, err = gamePath(game, path)
		}
		if err != nil {
			return "", err
		}
	case strings.HasPrefix(uri, "jm://play/"):
		id, _, _ := strings.Cut(strings.TrimPrefix(uri, "jm://play/"), "/")
		id, err := url.PathUnescape(id)
		if err != nil || id == "" || id == "." || id == ".." || strings.ContainsAny(id, `/\`) {
			return "", fmt.Errorf("no play %s", id)
		}
		// Its markers are in session.json; whether the build it was made with is still the build, in build/.
		return fileStamp(filepath.Join(plays.Root(root), id, "session.json")) + " " + buildStamp(root), nil
	case strings.HasPrefix(uri, "jm://entity?"):
		u, err := url.Parse(uri)
		if err == nil {
			file, _, err = gamePath(root, u.Query().Get("scene"))
		}
		if err != nil {
			return "", err
		}
	case uri == "jm://memory":
		data, _ := json.Marshal(memory.Notes(root))
		return fileEtag(data), nil
	case uri == "jm://schema" || strings.HasPrefix(uri, "jm://docs/"):
		return "-", nil
	default:
		if _, ok := readWidget(uri); ok {
			return "-", nil
		}
		return "", fmt.Errorf("unknown resource %s", uri)
	}
	return fileStamp(file), nil
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

// argumentValues are what a template's argument can be: games, a game's
// files, the open game's plays, a play's markers, scenes, entity names, topics.
func (s *mcpServer) argumentValues(name string, given map[string]string) []string {
	root := s.opened()
	var out []string
	switch name {
	case "game":
		home := s.home
		listed := false
		for _, p := range projects.List(home) {
			if p.Missing {
				continue
			}
			listed = listed || evalOrSelf(p.Path) == evalOrSelf(root)
			if filepath.Dir(p.Path) == evalOrSelf(home) {
				out = append(out, filepath.Base(p.Path)) // what findGame takes by name
			} else {
				out = append(out, p.Path)
			}
		}
		if root != "" && !listed {
			out = append(out, root) // the open game (from a root, say) though it isn't in the list
		}
	case "path":
		if game := given["game"]; game != "" {
			root, _ = findGame(s.home, game)
		}
		out = filesOf(root, "")
	case "play":
		all, _ := plays.List(root)
		for _, p := range all {
			out = append(out, p.ID)
		}
	case "marker":
		if root == "" {
			break
		}
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
	case "topic":
		for _, t := range docs.Topics() {
			out = append(out, t.Name)
		}
	}
	return out
}

// filesOf is the game at root's files ending in suffix, from its folder ("" root: none).
func filesOf(root, suffix string) []string {
	var out []string
	if root != "" {
		_ = walkProject(root, func(rel string, _ fs.FileInfo) {
			if strings.HasSuffix(rel, suffix) {
				out = append(out, rel)
			}
		})
	}
	return out
}

// projectResources lists the files an agent reads and writes, and the schema.
func projectResources(root string) []map[string]any {
	resources := []map[string]any{{"uri": "jm://schema", "name": "schema", "mimeType": "application/json",
		"description": "every component's scene keys and script fields (jm schema)"},
		{"uri": "jm://memory", "name": "memory", "mimeType": "application/json", "description": "the game's design notes (design/): each one's title, observations and links"}}
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

func readResource(root, uri string) (string, string, error) {
	if uri == "jm://schema" {
		out, failed := runJM(nil, root, "schema")
		if failed {
			return "", "", fmt.Errorf("%s", out)
		}
		return out, "application/json", nil
	}
	if uri == "jm://memory" {
		data, err := json.Marshal(map[string]any{"notes": memory.Notes(root)})
		return string(data), "application/json", err
	}
	if topic, ok := strings.CutPrefix(uri, "jm://docs/"); ok {
		text, err := docs.Read(topic)
		return text, "text/markdown", err
	}
	path, ok := strings.CutPrefix(uri, "file://")
	if !ok {
		return "", "", fmt.Errorf("unknown resource %s", uri)
	}
	path, err := url.PathUnescape(path)
	if err != nil {
		return "", "", err
	}
	// The files projectResources lists, and no others.
	real, rel, err := gamePath(root, path)
	if err != nil {
		return "", "", err
	}
	if mimeOf(rel) == "" {
		return "", "", fmt.Errorf("%s isn't one of the project's resources", uri)
	}
	data, err := os.ReadFile(real)
	if err != nil {
		return "", "", err
	}
	return string(data), mimeOf(path), nil
}
