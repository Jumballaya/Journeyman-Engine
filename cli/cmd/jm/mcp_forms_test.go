package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"image"
	"image/png"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

// formHost stands in for the client: it answers the forms s shows with
// replies in turn (then closes them) and keeps what it was asked.
func formHost(t *testing.T, s *mcpServer, forms formSupport, replies ...map[string]any) *[]map[string]any {
	t.Helper()
	r, w := io.Pipe()
	t.Cleanup(func() { w.Close() })
	s.peer, s.forms = newMCPPeer(w), forms
	asked := &[]map[string]any{}
	go func() {
		lines := bufio.NewScanner(r)
		for lines.Scan() {
			var req map[string]any
			json.Unmarshal(lines.Bytes(), &req)
			*asked = append(*asked, req)
			reply := map[string]any{"action": "cancel"}
			if len(*asked) <= len(replies) {
				reply = replies[len(*asked)-1]
			}
			data, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": req["id"], "result": reply})
			s.peer.deliver(data)
		}
	}()
	return asked
}

func accept(content map[string]any) map[string]any {
	return map[string]any{"action": "accept", "content": content}
}

// toolOf is the named tool, run as a call is: holding the calls lock.
func toolOf(t *testing.T, s *mcpServer, name string) mcpTool {
	for _, tool := range s.tools {
		if tool.Name == name {
			run := tool.run
			tool.run = func(a toolArgs) toolResult {
				s.calls.Lock()
				defer s.calls.Unlock()
				return run(a)
			}
			return tool
		}
	}
	t.Fatalf("no tool %s", name)
	return mcpTool{}
}

func TestAClientSaysWhichFormsItShows(t *testing.T) {
	for params, want := range map[string]formSupport{
		`{"capabilities":{"extensions":{"openai/elicitation":{"form":{}}},"elicitation":{}}}`: openaiForms,
		`{"capabilities":{"elicitation":{}}}`:                                                 plainForms,
		`{"capabilities":{"elicitation":{"form":{}}}}`:                                        plainForms,
		`{"capabilities":{"elicitation":{"url":{}}}}`:                                         noForms,
		`{"capabilities":{"extensions":{"openai/elicitation":{"userVerification":{}}}}}`:      noForms,
		`{"capabilities":{}}`: noForms,
	} {
		var init struct {
			Capabilities clientCaps `json:"capabilities"`
		}
		json.Unmarshal([]byte(params), &init)
		if got := formsOf(init.Capabilities, false); got != want {
			t.Errorf("%s: %v, want %v", params, got, want)
		}
	}
}

// The new-game wizard names the game and shapes its picture, in ChatGPT's
// form (a suggested name) or a plain one (choices as an enum).
func TestNewGameAsksTheNameAndPicture(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	t.Chdir(t.TempDir())
	os.Mkdir(filepath.Join(home, "My Game"), 0o755)
	for _, forms := range []formSupport{openaiForms, plainForms} {
		s := newMCPServer()
		name := map[formSupport]string{openaiForms: "Pix", plainForms: "Plain"}[forms]
		asked := formHost(t, s, forms, accept(map[string]any{"name": name, "picture": "pixel"}))
		if got := toolOf(t, s, "new_game").run(toolArgs{}); got.Failed {
			t.Fatal(got.Text)
		}
		req := (*asked)[0]
		schema := req["params"].(map[string]any)["requestedSchema"].(map[string]any)["properties"].(map[string]any)
		nameField, picture := schema["name"].(map[string]any), schema["picture"].(map[string]any)
		switch forms {
		case openaiForms:
			if req["method"] != "openai/elicitation/create" || nameField["default"] != "My Game 2" || nameField["x-openai-suggestions"] == nil || picture["oneOf"] == nil {
				t.Errorf("ChatGPT's form: %v", req)
			}
		case plainForms:
			if req["method"] != "elicitation/create" || nameField["x-openai-suggestions"] != nil || jsonOf(picture["enum"]) != `["wide","pixel","tall"]` {
				t.Errorf("plain form: %v", req)
			}
		}
		man, err := manifest.LoadManifest(filepath.Join(home, name, ".jm.json"))
		window := man.Config["window"].(map[string]any)
		if err != nil || man.Name != name || window["width"] != 1440.0 || man.Config["renderer"].(map[string]any)["logicalWidth"] != 480.0 {
			t.Errorf("%s: %+v %v", name, man, err)
		}
	}
}

func TestNewGameWithoutFormsNeedsAName(t *testing.T) {
	scratchSettings(t)
	t.Setenv("JM_GAMES", t.TempDir())
	t.Chdir(t.TempDir())
	s := newMCPServer()
	if got := toolOf(t, s, "new_game").run(toolArgs{}); !got.Failed || !strings.Contains(got.Text, "name") {
		t.Errorf("no forms: %s", got.Text)
	}
	formHost(t, s, openaiForms) // the person closes it
	if got := toolOf(t, s, "new_game").run(toolArgs{}); got.Failed || !strings.Contains(got.Text, "no game made") {
		t.Errorf("closed: %s", got.Text)
	}
}

// Exporting asks the platform, then how a Mac app is signed, from the editor's preferences.
func TestExportAsksWhereAndHowToSign(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{"name": "Neon Vow"}`})
	settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"editor.exportSignIdentity": "Developer ID Application: Me", "editor.exportNotarizeProfile": "jm"}})
	s := newMCPServer()
	if d := toolOf(t, s, "project_export").Description; !strings.Contains(d, "in a form") {
		t.Errorf("project_export doesn't say it asks: %s", d)
	}
	formHost(t, s, openaiForms, accept(map[string]any{"target": "darwin-arm64"}), accept(map[string]any{"signing": "notarize"}))
	s.calls.Lock()
	defer s.calls.Unlock()
	args, outcome := s.exportForm(".")
	want := "export --target darwin-arm64 --sign Developer ID Application: Me --notarize jm"
	if runtime.GOOS != "darwin" {
		want = "export --target darwin-arm64" // no keychain to sign with
	}
	if outcome != formAnswered || strings.Join(args, " ") != want {
		t.Errorf("args %q (%v), want %q", strings.Join(args, " "), outcome, want)
	}
	asked := formHost(t, s, openaiForms, accept(map[string]any{"target": "windows-amd64"}))

	if args, _ := s.exportForm("."); strings.Join(args, " ") != "export --target windows-amd64" || len(*asked) != 1 {
		t.Errorf("windows: %v, asked %d", args, len(*asked))
	}
	s.peer.close()
	if args, outcome := s.exportForm("."); outcome != formDeclined || args != nil {
		t.Errorf("the client gone is no export: %v %v", args, outcome)
	}
}

// The notary profile the signing form shows is the one used, though the
// preferences change while the person answers.
func TestExportNotarizesWithTheProfileShown(t *testing.T) {
	if runtime.GOOS != "darwin" {
		t.Skip("signing is asked on a Mac only")
	}
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{"name": "Neon Vow"}`})
	for _, later := range []string{"PROFILE-TWO", ""} {
		settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"editor.exportNotarizeProfile": "PROFILE-ONE"}})
		s := newMCPServer()
		r, w := io.Pipe()
		t.Cleanup(func() { w.Close() })
		s.peer, s.forms = newMCPPeer(w), openaiForms
		go func() {
			lines := bufio.NewScanner(r)
			for i := 0; lines.Scan(); i++ {
				var req map[string]any
				json.Unmarshal(lines.Bytes(), &req)
				reply := accept(map[string]any{"target": "darwin-arm64"})
				if i == 1 { // the signing form, while it's open
					settingsCall(t, settingsUpdateTool, map[string]any{"set": map[string]any{"editor.exportNotarizeProfile": later}})
					reply = accept(map[string]any{"signing": "notarize"})
				}
				data, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": req["id"], "result": reply})
				s.peer.deliver(data)
			}
		}()
		s.calls.Lock()
		args, _ := s.exportForm(".")
		s.calls.Unlock()
		if got := strings.Join(args, " "); !strings.HasSuffix(got, "--notarize PROFILE-ONE") {
			t.Errorf("changed to %q: %s", later, got)
		}
	}
}

// pick offers the game's files of a kind, scenes previewed in scene_view and
// textures with their picture, and gives back the picked ones' paths.
func TestPickOffersFilesWithPreviews(t *testing.T) {
	scratchSettings(t)
	var img bytes.Buffer
	png.Encode(&img, image.NewRGBA(image.Rect(0, 0, 4, 4)))
	inProject(t, map[string]string{".jm.json": `{}`, "a.scene.json": `{}`, "b.scene.json": `{}`, "hero.png": img.String(),
		"hero.ts": "", "hero.spec.ts": "", "env.d.ts": ""})
	s := newMCPServer()
	pick := toolOf(t, s, "pick")
	if got := pick.run(toolArgs{"kind": "scene"}); !strings.Contains(got.Text, "a.scene.json\nb.scene.json") {
		t.Errorf("no forms: the choices to ask about: %s", got.Text)
	}
	if got := pick.run(toolArgs{"kind": "script"}); !strings.Contains(got.Text, "hero.ts") || strings.Contains(got.Text, "spec.ts") || strings.Contains(got.Text, "d.ts") {
		t.Errorf("scripts are scripts, not tests or declarations: %s", got.Text)
	}
	abs := filepath.Join(evalOrSelf(here(t)), "b.scene.json") // as offered
	asked := formHost(t, s, openaiForms, accept(map[string]any{"picked": []any{fileURI(abs)}}))
	got := pick.run(toolArgs{"kind": "scene", "many": true})
	if picked := got.Structured.(map[string]any)["picked"]; jsonOf(picked) != `["b.scene.json"]` {
		t.Errorf("picked: %v (%s)", picked, got.Text)
	}
	field := (*asked)[0]["params"].(map[string]any)["requestedSchema"].(map[string]any)["properties"].(map[string]any)["picked"].(map[string]any)
	input := field["x-openai-input"].(map[string]any)
	preview := input["options"].([]any)[0].(map[string]any)["_meta"].(map[string]any)["openai/preview"].(map[string]any)["target"].(map[string]any)
	if field["type"] != "array" || input["selection"] != "explicit" || preview["name"] != "scene_view" {
		t.Errorf("picker: %v", field)
	}
	asked = formHost(t, s, openaiForms, accept(map[string]any{"picked": "x"}))
	pick.run(toolArgs{"kind": "texture"})
	option := (*asked)[0]["params"].(map[string]any)["requestedSchema"].(map[string]any)["properties"].(map[string]any)["picked"].(map[string]any)["x-openai-input"].(map[string]any)["options"].([]any)[0].(map[string]any)
	if !strings.HasPrefix(jsonOf(option["_meta"].(map[string]any)["openai/thumbnail"]), `{"src":"data:image/jpeg;base64,`) {
		t.Errorf("a texture shows itself: %v", option["_meta"])
	}
}

// A scene's preview stays the scene of the game picked in, whichever game opens after.
func TestPickPreviewStaysInItsGame(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	for _, g := range []string{"A", "B"} {
		filesGame(t, filepath.Join(home, g))
		os.WriteFile(filepath.Join(home, g, "level.scene.json"), []byte(`{"entities": [{"name": "From`+g+`"}]}`), 0o644)
	}
	s := newMCPServer()
	s.openGame(filepath.Join(home, "A"))
	args := pickField(s.opened(), []string{"level.scene.json"}, false)["x-openai-input"].(map[string]any)["options"].([]map[string]any)[0]["_meta"].(map[string]any)["openai/preview"].(map[string]any)["target"].(map[string]any)["arguments"].(map[string]any)
	s.openGame(filepath.Join(home, "B"))
	if got := toolOf(t, s, "scene_view").run(toolArgs(args)); got.Failed || !strings.Contains(got.Text, "FromA") {
		t.Errorf("after another game opened: %s", got.Text)
	}
}

// Over stdio the server reads the client's answer to its form while the tool
// waits for it, and answers the client meanwhile (a picker's preview).
func TestAFormIsAnsweredWhileItsToolWaits(t *testing.T) {
	scratchSettings(t)
	inProject(t, map[string]string{".jm.json": `{}`, "a.scene.json": `{}`})
	inR, inW := io.Pipe()
	outR, outW := io.Pipe()
	go func() { serveMCP(inR, outW); outW.Close() }()
	lines := bufio.NewScanner(outR)
	next := func() map[string]any {
		if !lines.Scan() {
			t.Fatal("the server stopped")
		}
		var msg map[string]any
		json.Unmarshal(lines.Bytes(), &msg)
		return msg
	}
	io.WriteString(inW, `{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{"extensions":{"openai/elicitation":{"form":{}}}}}}`+"\n")
	next()
	io.WriteString(inW, `{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"pick","arguments":{"kind":"scene"}}}`+"\n")
	form := next()
	if form["method"] != "openai/elicitation/create" {
		t.Fatalf("not a form: %v", form)
	}
	io.WriteString(inW, `{"jsonrpc":"2.0","id":3,"method":"resources/read","params":{"uri":"`+sceneWidget.appURI()+`"}}`+"\n")
	if preview := next(); preview["id"] != 3.0 {
		t.Fatalf("a preview waits for the picker: %v", preview)
	}
	abs := filepath.Join(evalOrSelf(here(t)), "a.scene.json") // as offered
	reply, _ := json.Marshal(map[string]any{"jsonrpc": "2.0", "id": form["id"], "result": accept(map[string]any{"picked": fileURI(abs)})})
	inW.Write(append(reply, '\n'))
	if result := next(); !strings.Contains(jsonOf(result["result"]), "The person picked: a.scene.json") {
		t.Errorf("tool result: %v", result)
	}
	inW.Close()
}

// With examples to start from, the wizard shows them first (pictures and a
// line each), and the game is a renamed copy of the one picked.
func TestNewGameCanStartFromAnExample(t *testing.T) {
	scratchSettings(t)
	home := t.TempDir()
	t.Setenv("JM_GAMES", home)
	t.Chdir(t.TempDir())
	defer func(v string) { version = v }(version)
	version = "v9.9.9"
	example := filepath.Join(os.Getenv("HOME"), ".jm", "examples", version, "tetris")
	os.MkdirAll(filepath.Join(example, "tests", "golden", "start"), 0o755)
	os.WriteFile(filepath.Join(example, ".jm.json"), []byte(`{"name": "Tetris", "scenes": []}`), 0o644)
	os.WriteFile(filepath.Join(example, "README.md"), []byte("# Tetris\n\nGuideline-style Tetris built\non Journeyman. Hold and ghost.\n"), 0o644)
	var img bytes.Buffer
	png.Encode(&img, image.NewRGBA(image.Rect(0, 0, 4, 4)))
	os.WriteFile(filepath.Join(example, "tests", "golden", "start", "frame_1.png"), img.Bytes(), 0o644)

	s := newMCPServer()
	asked := formHost(t, s, openaiForms, accept(map[string]any{"from": "tetris"}), accept(map[string]any{"name": "Blocks"}))
	if got := toolOf(t, s, "new_game").run(toolArgs{}); got.Failed {
		t.Fatal(got.Text)
	}
	from := (*asked)[0]["params"].(map[string]any)["requestedSchema"].(map[string]any)["properties"].(map[string]any)["from"].(map[string]any)
	tetris := from["oneOf"].([]any)[1].(map[string]any)
	if tetris["description"] != "Guideline-style Tetris built on Journeyman." || tetris["x-openai-thumbnail"] == nil {
		t.Errorf("example option: %v", tetris)
	}
	second := (*asked)[1]["params"].(map[string]any)["requestedSchema"].(map[string]any)["properties"].(map[string]any)
	if second["name"].(map[string]any)["default"] != "Tetris" || second["picture"] != nil {
		t.Errorf("second round: %v", second)
	}
	if man, err := manifest.LoadManifest(filepath.Join(home, "Blocks", ".jm.json")); err != nil || man.Name != "Blocks" {
		t.Errorf("the copy: %+v %v", man, err)
	}
}
