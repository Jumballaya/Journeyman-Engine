package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"image"
	"image/png"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// mcpSession sends requests (one JSON-RPC message per line) and returns the
// replies by id.
func mcpSession(t *testing.T, requests ...string) map[float64]map[string]any {
	t.Helper()
	var out bytes.Buffer
	if err := serveMCP(strings.NewReader(strings.Join(requests, "\n")), &out); err != nil {
		t.Fatal(err)
	}
	replies := map[float64]map[string]any{}
	for _, line := range strings.Split(strings.TrimSpace(out.String()), "\n") {
		var msg map[string]any
		if err := json.Unmarshal([]byte(line), &msg); err != nil {
			t.Fatalf("not JSON: %s", line)
		}
		id, _ := msg["id"].(float64)
		replies[id] = msg
	}
	return replies
}

func TestMCPInitializesAndListsTools(t *testing.T) {
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"t","version":"0"}}}`,
		`{"jsonrpc":"2.0","method":"notifications/initialized"}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/list"}`,
		`{"jsonrpc":"2.0","id":3,"method":"nope"}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"drive","arguments":{"command":"step"}}}`)
	if len(replies) != 4 {
		t.Fatalf("a notification gets no reply: got %d replies", len(replies))
	}
	info := replies[1]["result"].(map[string]any)
	if info["protocolVersion"] != mcpProtocolVersion || info["serverInfo"].(map[string]any)["name"] != "journeyman" {
		t.Fatalf("initialize: %v", info)
	}
	names := []string{}
	for _, tool := range replies[2]["result"].(map[string]any)["tools"].([]any) {
		names = append(names, tool.(map[string]any)["name"].(string))
	}
	if strings.Join(names, ",") != "games,games_find,new_game,open_game,project_open_editor,project_play,project_export,build,doctor,test,golden,schema,generate,fmt,export,drive_start,drive,drive_frame,drive_profile,drive_stop,session,plays_list,play_show,play_frame,play_moment,play_state,play_inputs,play_verify,play_resume,mention_search,scene_view,scene_render,studio,files,file_read,file_write,file_move,file_delete,files_search,file_open,asset_add,memory,memory_search,memory_note,dossier_new,memory_decide,world_context,world_path,world_link,vcs_status,vcs_diff,vcs_commit,vcs_log,vcs_restore,variants,variant_new,variant_play,variant_compare,variant_merge,variant_drop,sandboxes,sandbox_new,sandbox_run,sandbox_promote,sandbox_drop,tasks,task_add,task_move,task_remove,editor_state,editor_show,settings_read,settings_update,pick,companions,play_live,game_index,health,file_view,game_log" {
		t.Fatalf("tools: %v", names)
	}
	for _, tool := range replies[2]["result"].(map[string]any)["tools"].([]any) {
		if tool := tool.(map[string]any); tool["name"] == "play_inputs" {
			props := tool["inputSchema"].(map[string]any)["properties"].(map[string]any)
			from, to := props["from"].(map[string]any)["description"].(string), props["to"].(map[string]any)["description"].(string)
			if !strings.Contains(from, `"start" (default)`) || !strings.Contains(to, `"end" (default)`) {
				t.Errorf("play_inputs defaults to the whole play: from %q, to %q", from, to)
			}
		}
	}
	if replies[3]["error"].(map[string]any)["code"].(float64) != -32601 {
		t.Fatalf("an unknown method is an error: %v", replies[3])
	}
	result := replies[4]["result"].(map[string]any)
	if result["isError"] != true || !strings.Contains(result["content"].([]any)[0].(map[string]any)["text"].(string), "drive_start") {
		t.Fatalf("driving with no game running should say to start one: %v", result)
	}
}

// Resources are the project's files (not build/ or node_modules), read only inside the project.
func TestMCPResourcesAreTheProjectsFiles(t *testing.T) {
	dir := t.TempDir()
	for path, text := range map[string]string{
		".jm.json": "{}", "scenes/main.scene.json": "{}", "assets/scripts/hero.ts": "// hero",
		"build/scenes/main.scene.json": "{}", "assets/scripts/node_modules/x/index.ts": "", "assets/hero.png": "",
	} {
		full := filepath.Join(dir, path)
		if err := os.MkdirAll(filepath.Dir(full), 0755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(text), 0644); err != nil {
			t.Fatal(err)
		}
	}
	t.Chdir(dir)
	names := []string{}
	for _, r := range projectResources() {
		if name := r["name"].(string); !strings.HasPrefix(name, "docs/") {
			names = append(names, name)
		}
	}
	if strings.Join(names, ",") != "schema,memory,.jm.json,assets/scripts/hero.ts,scenes/main.scene.json" {
		t.Fatalf("resources: %v", names)
	}
	abs, _ := filepath.Abs("assets/scripts/hero.ts")
	if text, mime, err := readResource("file://" + filepath.ToSlash(abs)); err != nil || text != "// hero" || mime != "text/x-typescript" {
		t.Fatalf("read: %q %q %v", text, mime, err)
	}
	if text, mime, err := readResource("jm://docs/agents"); err != nil || !strings.Contains(text, "jm build --json") || mime != "text/markdown" {
		t.Fatalf("docs: %q %v", mime, err)
	}
	if _, _, err := readResource("file:///etc/hosts"); err == nil {
		t.Fatal("files outside the project can't be read")
	}
}

// fakeDriver gives s a game whose engine answers each command line with
// answer(command), its folder work.
func fakeDriver(s *mcpServer, work string, answer func(string) string) {
	cmdR, cmdW := io.Pipe()
	replyR, replyW := io.Pipe()
	go func() {
		lines := bufio.NewScanner(cmdR)
		for lines.Scan() {
			fmt.Fprintln(replyW, answer(lines.Text()))
		}
		replyW.Close()
	}()
	s.game = &drivenGame{cmd: &exec.Cmd{}, in: cmdW, out: bufio.NewScanner(replyR), work: work}
}

func TestADriveBatchCantFloodTheContext(t *testing.T) {
	s := newMCPServer()
	big := `{"ok":true,"state":"` + strings.Repeat("x", 400) + `"}`
	fakeDriver(s, t.TempDir(), func(c string) string {
		if c == "boom" {
			return `{"ok":false,"error":"no such command"}`
		}
		return big
	})
	r := s.driveBatch(nil, []string{"state"}, 100, 2000)
	out, failed := r.Text, r.Failed
	lines := strings.Split(out, "\n")
	if failed || lines[len(lines)-1] != `{"truncated":true,"after":5}` || len(lines) != 6 {
		t.Errorf("past the limit: failed=%v, %d lines ending %s", failed, len(lines), lines[len(lines)-1])
	}
	r = s.driveBatch(nil, []string{"step 1", "boom", "step 1"}, 3, 1<<20)
	if out, failed = r.Text, r.Failed; !failed || strings.Count(out, "\n") != 1 {
		t.Errorf("a failure should stop the batch with the replies so far: failed=%v\n%s", failed, out)
	}
}

func TestDriveFrameShowsTheGameWithNoFileToManage(t *testing.T) {
	s := newMCPServer()
	work := filepath.Join(t.TempDir(), "game")
	os.Mkdir(work, 0o755)
	fakeDriver(s, work, func(c string) string {
		if path, ok := strings.CutPrefix(c, "capture "); ok {
			f, _ := os.Create(path)
			png.Encode(f, image.NewRGBA(image.Rect(0, 0, 64, 36)))
			f.Close()
			return `{"ok":true,"path":"` + path + `"}`
		}
		return `{"ok":true,"frame":43}`
	})
	s.driveCommand("step 43")
	r := s.driveFrame(nil)
	if r.Failed || len(r.Images) != 1 || r.Images[0].MimeType != "image/jpeg" || !strings.Contains(r.Text, "Frame 42") {
		t.Fatalf("drive_frame: %+v", r.Text)
	}
	// A batch's capture attaches the frame too, saved in the driver's folder whatever path it names.
	r = s.driveBatch(nil, []string{"step 1", "capture /etc/passwd", "capture"}, 1, 1<<20)
	if r.Failed || len(r.Images) != 2 || !strings.Contains(r.Text, `"image":"attached"`) {
		t.Fatalf("capture in a batch: %v %d images\n%s", r.Failed, len(r.Images), r.Text)
	}
	s.stopDriver()
	if _, err := os.Stat(work); !os.IsNotExist(err) {
		t.Error("the driver's folder outlived it")
	}

	fakeDriver(s, t.TempDir(), func(string) string {
		return `{"ok":false,"error":"no pixels with JM_RENDERER=none (state has the draw list)"}`
	})
	if r := s.driveFrame(nil); !r.Failed || !strings.Contains(r.Text, "gl: true") {
		t.Errorf("without GL: %s", r.Text)
	}
}

func TestDriveStopNamesThePlayItRecorded(t *testing.T) {
	s := newMCPServer()
	fakeDriver(s, t.TempDir(), func(string) string { return `{"ok":true}` })
	s.game.play = filepath.Join(t.TempDir(), ".jm", "plays", "2000-01-01_000000")
	var stop mcpTool
	for _, tool := range s.tools {
		if tool.Name == "drive_stop" {
			stop = tool
		}
	}
	r := stop.run(toolArgs{})
	if !strings.Contains(r.Text, `"play":"2000-01-01_000000"`) || s.game != nil {
		t.Errorf("drive_stop: %s", r.Text)
	}
}

func TestDriveNeedsACommand(t *testing.T) {
	s := newMCPServer()
	for _, tool := range s.tools {
		if tool.Name == "drive" {
			if r := tool.run(toolArgs{}); !r.Failed || !strings.Contains(r.Text, "give a command") {
				t.Errorf("drive with nothing: %s", r.Text)
			}
		}
	}
}

func TestMCPFmtAndExportRunJM(t *testing.T) {
	t.Chdir(t.TempDir())
	os.MkdirAll("scenes", 0o755)
	os.WriteFile("scenes/a.scene.json", []byte(`{}`), 0o644)
	var ran []string
	saved := runJM
	runJM = func(_ *toolCall, a ...string) (string, bool) {
		ran = append(ran, strings.Join(a, " "))
		return "", false
	}
	defer func() { runJM = saved }()
	mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"fmt","arguments":{}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"fmt","arguments":{"check":true,"files":["scenes/a.scene.json"]}}}`,
		`{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"export","arguments":{}}}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"export","arguments":{"target":"windows-amd64","server":true}}}`)
	want := "fmt|fmt --check -- scenes/a.scene.json|export|export --target windows-amd64 --server"
	if got := strings.Join(ran, "|"); got != want {
		t.Errorf("ran %q; want %q", got, want)
	}
}

func TestABuildIsStaleWhenMissingOrOlderThanItsSources(t *testing.T) {
	root := t.TempDir()
	os.WriteFile(filepath.Join(root, ".jm.json"), []byte(`{}`), 0o644)
	os.MkdirAll(filepath.Join(root, "assets", "scripts", "node_modules"), 0o755)
	if !buildIsStale(root) {
		t.Error("no build isn't stale")
	}
	os.MkdirAll(filepath.Join(root, "build"), 0o755)
	os.WriteFile(filepath.Join(root, "build", ".jm.json"), []byte(`{}`), 0o644)
	old := time.Now().Add(-time.Hour)
	os.Chtimes(filepath.Join(root, ".jm.json"), old, old)
	if buildIsStale(root) {
		t.Error("a build newer than its sources is stale")
	}
	os.WriteFile(filepath.Join(root, "assets", "scripts", "node_modules", "x.js"), nil, 0o644)
	ahead := time.Now().Add(time.Minute)
	os.Chtimes(filepath.Join(root, "assets", "scripts", "node_modules", "x.js"), ahead, ahead)
	if buildIsStale(root) {
		t.Error("node_modules counts as a source")
	}
	os.WriteFile(filepath.Join(root, "assets", "player.ts"), nil, 0o644)
	later := time.Now().Add(time.Minute) // past the build's time, however coarse the clock
	os.Chtimes(filepath.Join(root, "assets", "player.ts"), later, later)
	if !buildIsStale(root) {
		t.Error("an edited script doesn't make the build stale")
	}
}

// The interview prompt is listed and gets its text; the memory tools write design/.
func TestMCPPromptsAndMemoryTools(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, ".jm.json"), []byte(`{"name":"Pip"}`), 0o644)
	t.Chdir(dir)
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"prompts/list"}`,
		`{"jsonrpc":"2.0","id":2,"method":"prompts/get","params":{"name":"new_game_interview"}}`,
		`{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"memory_note","arguments":{"text":"jumps feel exact","note":"Pillars","category":"pillar"}}}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"memory","arguments":{"note":"pillars"}}}`)
	if !strings.Contains(jsonOf(replies[1]), `"new_game_interview"`) || !strings.Contains(jsonOf(replies[2]), "memory_note") {
		t.Errorf("prompts: %v %v", replies[1], replies[2])
	}
	read := jsonOf(replies[4])
	if !strings.Contains(read, `- [pillar] jumps feel exact`) || !strings.Contains(read, `"backlinks":["Design"]`) || !strings.Contains(read, "obsidian://open?path=") {
		t.Errorf("memory: %s", read)
	}
}

// dossier_new writes a dossier linked by its props; memory lists the kinds,
// and with a kind, that kind's dossiers and their properties.
func TestMCPDossiers(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, ".jm.json"), []byte(`{"name":"Pip"}`), 0o644)
	t.Chdir(dir)
	replies := mcpSession(t,
		`{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"dossier_new","arguments":{"kind":"location","name":"Irongate","summary":"Capital of [[Varn]].","props":{"region":"[[Ashvale]]"}}}}`,
		`{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"dossier_new","arguments":{"kind":"character","name":"Mara Dorne","props":{"home":"[[Irongate]]"}}}}`,
		`{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"memory","arguments":{"kind":"location"}}}`,
		`{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"memory","arguments":{"note":"Irongate"}}}`,
		`{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"memory","arguments":{}}}`,
		`{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"dossier_new","arguments":{"kind":"spaceship","name":"X"}}}`)
	if made := jsonOf(replies[1]); !strings.Contains(made, `design/Locations/Irongate.md`) || !strings.Contains(made, `"region":"[[Ashvale]]"`) {
		t.Errorf("dossier_new: %s", made)
	}
	if list := jsonOf(replies[3]); !strings.Contains(list, "Irongate (design/Locations/Irongate.md)") || strings.Contains(list, "Mara") {
		t.Errorf("memory kind: %s", list)
	}
	if read := jsonOf(replies[4]); !strings.Contains(read, `"backlinks":["Mara Dorne"]`) {
		t.Errorf("Mara's home links Irongate: %s", read)
	}
	if all := jsonOf(replies[5]); !strings.Contains(all, "character (design/Characters/): role, faction, home, portrait") {
		t.Errorf("memory lists the kinds: %s", all)
	}
	if bad := replies[6]["result"].(map[string]any); bad["isError"] != true {
		t.Errorf("an unknown kind: %v", bad)
	}
	if _, err := os.Stat(filepath.Join(dir, "design", "Locations.base")); err != nil {
		t.Error("no base for Locations:", err)
	}
}

// The world's tools: a new capital linked into its region, what's around
// the region, how a family reaches the other nation, and a disagreement.
func TestMCPWorld(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, ".jm.json"), []byte(`{"name":"Pip"}`), 0o644)
	t.Chdir(dir)
	call := func(id int, name, args string) string {
		return fmt.Sprintf(`{"jsonrpc":"2.0","id":%d,"method":"tools/call","params":{"name":%q,"arguments":%s}}`, id, name, args)
	}
	replies := mcpSession(t,
		call(1, "dossier_new", `{"kind":"location","name":"Ashvale","summary":"The valley between Varn and [[Ost]]."}`),
		call(2, "dossier_new", `{"kind":"location","name":"Irongate","summary":"Varn's capital.","props":{"region":"[[Ashvale]]"}}`),
		call(3, "dossier_new", `{"kind":"location","name":"Duskhold","summary":"Ost's capital."}`),
		call(4, "world_link", `{"from":"Duskhold","type":"located_in","to":"Ashvale"}`),
		call(5, "world_link", `{"from":"Irongate","type":"road_to","to":"Duskhold","details":{"distance":"120 km"}}`),
		call(6, "world_link", `{"from":"Duskhold","type":"road_to","to":"Irongate","details":{"distance":"90 km"}}`),
		call(7, "world_context", `{"note":"ashvale"}`),
		call(8, "world_path", `{"from":"Irongate","to":"Ost"}`),
		call(9, "health", `{"checks":["world"]}`))
	if text := jsonOf(replies[6]); !strings.Contains(text, "disagrees with itself") || !strings.Contains(text, "90 km") {
		t.Errorf("a second length for the road: %s", text)
	}
	around := jsonOf(replies[7])
	for _, want := range []string{"Duskhold —located_in→ Ashvale", "Irongate —region→ Ashvale", "Irongate —road_to→ Duskhold (distance: 120 km)", "Linked, no note yet: Ost"} {
		if !strings.Contains(around, want) {
			t.Errorf("no %q in %s", want, around)
		}
	}
	if path := jsonOf(replies[8]); !strings.Contains(path, "Irongate —region→ Ashvale") || !strings.Contains(path, "Ashvale —mentions→ Ost") {
		t.Errorf("Irongate to Ost: %s", path)
	}
	if health := jsonOf(replies[9]); !strings.Contains(health, `"name":"world","ok":false`) || !strings.Contains(health, "design/Locations/") {
		t.Errorf("health: %s", health)
	}
}
