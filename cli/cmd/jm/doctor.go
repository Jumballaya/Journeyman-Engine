package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/Jumballaya/Journeyman-Engine/internal/plays"
	"github.com/Jumballaya/Journeyman-Engine/internal/schema"
	"github.com/Jumballaya/Journeyman-Engine/internal/toolchain"

	"github.com/spf13/cobra"
)

var doctorFetch bool

var doctorCmd = &cobra.Command{
	Use:   "doctor",
	Short: "Check that jm can build and run games here",
	Long: `Checks this machine (and the project in the current folder, if any): jm's
version, the engine it would run, whether it starts and matches, the engine's
schema, the install (jm first on PATH, the server beside it, no macOS
quarantine), and the script toolchain (Node.js and AssemblyScript: the
machine's or the project's own, else the copies jm downloads to ~/.jm/toolchains).

--fetch downloads whatever of the toolchain is missing now, rather than on the
first build (for an image or a CI cache). --json prints one JSON object:
{"ok", "jm", "engine", "toolchain", "project", "problems"}. Exits 1 when
something would stop a build or a run.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		r := diagnose(doctorFetch, cmd.ErrOrStderr())
		if jsonOutput {
			out, _ := json.MarshalIndent(r, "", "  ")
			fmt.Println(string(out))
		} else {
			r.print(cmd.OutOrStdout())
		}
		if !r.OK {
			os.Exit(1)
		}
		return nil
	},
}

func init() {
	doctorCmd.Flags().BoolVar(&jsonOutput, "json", false, "one JSON object (for tools)")
	doctorCmd.Flags().BoolVar(&doctorFetch, "fetch", false, "download the missing toolchain now")
}

type doctorReport struct {
	OK        bool                 `json:"ok"`
	JM        doctorJM             `json:"jm"`
	Engine    doctorEngine         `json:"engine"`
	Toolchain *toolchain.Toolchain `json:"toolchain"`
	Project   *doctorProject       `json:"project"`
	Problems  []doctorProblem      `json:"problems"`
}

type doctorJM struct {
	Version string `json:"version"`
	Path    string `json:"path"`
	Editor  string `json:"editor,omitempty"` // what jm editor opens
}

type doctorEngine struct {
	Path    string `json:"path,omitempty"`
	Version string `json:"version,omitempty"`
	Schema  bool   `json:"schema"` // answers --schema (scene checks, jm schema)
}

type doctorProject struct {
	Name   string `json:"name"`
	Root   string `json:"root"`
	Scenes int    `json:"scenes"`
	Built  bool   `json:"built"` // build/ exists
	Plays  int    `json:"plays"` // recorded plays (jm plays)
	Latest string `json:"latestPlay,omitempty"`
}

type doctorProblem struct {
	Level   string `json:"level"` // error: stops a build or run; warning: works, but
	Message string `json:"message"`
	Fix     string `json:"fix,omitempty"`
}

func (r *doctorReport) problem(level, message, fix string) {
	r.Problems = append(r.Problems, doctorProblem{level, message, fix})
	if level == "error" {
		r.OK = false
	}
}

func diagnose(fetch bool, log io.Writer) doctorReport {
	r := doctorReport{OK: true, JM: doctorJM{Version: version}, Problems: []doctorProblem{}}
	if self, err := executablePath(); err == nil {
		r.JM.Path = self
		r.JM.Editor, _ = findEditor()
		r.checkInstall(self)
	}

	if engine, err := projectEngine(""); err != nil {
		r.problem("error", err.Error(), "keep journeyman_engine beside jm, as a release has it")
	} else {
		r.Engine.Path, _ = filepath.Abs(engine)
		var runErr error
		r.Engine.Version, runErr = engineVersion(engine)
		switch {
		case runErr != nil:
			r.problem("error", fmt.Sprintf("the engine at %s doesn't run: %v", r.Engine.Path, runErr), unblockFix(filepath.Dir(r.Engine.Path)))
		case r.Engine.Version == "":
			r.problem("warning", "the engine doesn't say its version (older than v0.0.2)", "install the release jm came with")
		case version != "dev" && r.Engine.Version != version:
			r.problem("warning", fmt.Sprintf("the engine is %s but jm is %s", r.Engine.Version, version), "use the engine from jm's own release")
		}
		if _, err := schema.Raw(engine); err == nil {
			r.Engine.Schema = true
		} else {
			r.problem("warning", "the engine has no --schema: jm build can't check scenes and prefabs", "update the engine")
		}
	}

	scriptsDir := ""
	if exists(archive.ManifestEntryKey) {
		root, _ := os.Getwd()
		p := &doctorProject{Root: root, Built: exists(filepath.Join("build", archive.ManifestEntryKey))}
		if man, err := manifest.LoadManifest(archive.ManifestEntryKey); err != nil {
			r.problem("error", "the project's .jm.json doesn't load: "+err.Error(), "")
		} else {
			p.Name, p.Scenes = man.Name, len(man.Scenes)
		}
		var raw map[string]any
		if data, err := os.ReadFile(archive.ManifestEntryKey); err == nil && json.Unmarshal(data, &raw) == nil {
			if _, old := raw["engine"]; old {
				r.problem("warning", `.jm.json's "engine" is ignored: jm uses $JM_ENGINE, else the engine beside it, else PATH`,
					`remove "engine" from .jm.json`)
			}
		}
		if all, err := plays.List(root); err == nil {
			p.Plays = len(all)
			if len(all) > 0 {
				p.Latest = all[0].ID
			}
		}
		r.Project = p
		for _, file := range editorUnsaved(root) {
			r.problem("warning", file+" is open in the editor with unsaved edits", "save it in the editor before changing it here")
		}
		scriptsDir = scriptsPath(root)
	}

	if missing := unconnectedAgents(); len(missing) > 0 {
		r.problem("warning", "not connected to jm yet: "+strings.Join(missing, ", "), "jm setup (then restart desktop apps)")
	}

	tc, err := toolchain.Find(scriptsDir, fetch, log)
	if err != nil {
		// Not an error: the first build downloads it.
		r.problem("warning", err.Error(), "jm doctor --fetch, or just build (it downloads on first use)")
	} else {
		r.Toolchain = &tc
	}
	return r
}

// engineVersion asks the engine (`--version`, from v0.0.2); "" when it doesn't
// answer. Older engines would take the flag for a game to run, so it gets a
// moment, no window, and is stopped. An error means it couldn't run at all.
func engineVersion(engine string) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, engine, "--version")
	cmd.Env = append(os.Environ(), "JM_RENDERER=none")
	out, err := cmd.Output()
	var exit *exec.ExitError
	if ctx.Err() != nil || errors.As(err, &exit) && exit.Exited() {
		return "", nil // an older engine took --version for a game: it ran, or is running
	}
	if err != nil {
		return "", err // couldn't start, or the system killed it
	}
	v, _ := strings.CutPrefix(strings.TrimSpace(string(out)), "journeyman_engine ")
	if v == strings.TrimSpace(string(out)) {
		return "", nil
	}
	return v, nil
}

// checkInstall finds what stops an install that works elsewhere: another jm
// first on PATH, or no server beside it.
func (r *doctorReport) checkInstall(self string) {
	dir := filepath.Dir(self)
	if onPath, err := exec.LookPath("jm"); err != nil {
		r.problem("warning", "jm isn't on PATH", fmt.Sprintf(`export PATH=%s:"$PATH" (and add that line to your shell profile)`, shellQuote(dir)))
	} else if !sameFile(onPath, self) {
		r.problem("warning", fmt.Sprintf("PATH runs another jm first: %s, not %s", onPath, self), "remove the other one, or put "+dir+" first on PATH")
	}
	if _, err := resolveServerPath(); err != nil {
		r.problem("warning", "no journeyman_server: jm export --server and dedicated servers won't work", "keep journeyman_server beside jm, as a release has it")
	}
}

// unblockFix is what to try when a program in dir won't start. On macOS
// that's usually the quarantine a browser download carries.
func unblockFix(dir string) string {
	if runtime.GOOS == "darwin" {
		return "xattr -dr com.apple.quarantine " + shellQuote(dir) + " (else reinstall: the install.sh line on the download page)"
	}
	return "reinstall: the install line on the download page"
}

func shellQuote(s string) string {
	return "'" + strings.ReplaceAll(s, "'", `'\''`) + "'"
}

func sameFile(a, b string) bool {
	ia, errA := os.Stat(a)
	ib, errB := os.Stat(b)
	return errA == nil && errB == nil && os.SameFile(ia, ib)
}

func exists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func (r doctorReport) print(w io.Writer) {
	fmt.Fprintf(w, "jm         %s (%s)\n", r.JM.Version, r.JM.Path)
	if r.JM.Editor != "" {
		fmt.Fprintf(w, "editor     %s\n", r.JM.Editor)
	}
	if r.Engine.Path != "" {
		v := r.Engine.Version
		if v == "" {
			v = "unknown version"
		}
		fmt.Fprintf(w, "engine     %s (%s)\n", v, r.Engine.Path)
	}
	if tc := r.Toolchain; tc != nil {
		fmt.Fprintf(w, "node       %s, %s (%s)\n", tc.NodeVer, tc.NodeSource, tc.Node)
		fmt.Fprintf(w, "asc        %s (%s)\n", tc.ASCSource, tc.ASC)
	}
	if p := r.Project; p != nil {
		built := "not built yet"
		if p.Built {
			built = "built"
		}
		fmt.Fprintf(w, "project    %s: %d scene(s), %s, %d recorded play(s)\n", p.Name, p.Scenes, built, p.Plays)
	}
	for _, p := range r.Problems {
		fmt.Fprintf(w, "%-10s %s\n", p.Level+":", p.Message)
		if p.Fix != "" {
			fmt.Fprintf(w, "           fix: %s\n", p.Fix)
		}
	}
	if r.OK {
		fmt.Fprintln(w, "OK")
	}
}
