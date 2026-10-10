package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
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
version, the engine it would run and whether it matches, the engine's schema,
and the script toolchain (Node.js and AssemblyScript: the machine's or the
project's own, else the copies jm downloads to ~/.jm/toolchains).

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
	Editor  string `json:"editor,omitempty"` // beside jm, in the human pack
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
		for _, name := range []string{"journeyman_editor", "journeyman_editor.exe", "Journeyman Editor.app"} {
			if p := filepath.Join(filepath.Dir(self), name); exists(p) {
				r.JM.Editor = p
			}
		}
	}

	if engine, err := projectEngine(""); err != nil {
		r.problem("error", err.Error(), "keep journeyman_engine beside jm, as a release has it")
	} else {
		r.Engine.Path, _ = filepath.Abs(engine)
		r.Engine.Version = engineVersion(engine)
		switch {
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
// moment, no window, and is stopped.
func engineVersion(engine string) string {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, engine, "--version")
	cmd.Env = append(os.Environ(), "JM_RENDERER=none")
	out, err := cmd.Output()
	v, ok := strings.CutPrefix(strings.TrimSpace(string(out)), "journeyman_engine ")
	if err != nil || !ok {
		return ""
	}
	return v
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
