package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/Jumballaya/Journeyman-Engine/internal/projects"
	"github.com/Jumballaya/Journeyman-Engine/internal/vcs"

	"github.com/spf13/cobra"
)

var vcsFrom string
var vcsCount int

var vcsCmd = &cobra.Command{
	Use:   "vcs",
	Short: "The game's history (git): what changed, commit, the versions before, go back",
	Long: `The game's versions, kept in git (jm init makes the first commit), about
the game's folder even inside a bigger repository. jm vcs lists what changed
since the last commit, a scene's changes per entity. Restore stashes what it
would lose first; commit with no message names the files. For a line diff:
git diff (over MCP, vcs_diff).`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		return inGameRoot(func(root string) error {
			changes := gameChanges(root)
			if jsonOutput {
				return writeJSON(cmd.OutOrStdout(), map[string]any{"changes": changes})
			}
			fmt.Fprint(cmd.OutOrStdout(), changesText(changes))
			return nil
		})
	},
}

func init() {
	vcsCmd.PersistentFlags().BoolVar(&jsonOutput, "json", false, "JSON output (for tools)")
	sub := func(use, short string, args cobra.PositionalArgs, run func(w io.Writer, root string, args []string) error) *cobra.Command {
		c := &cobra.Command{Use: use, Short: short, Args: args, RunE: func(cmd *cobra.Command, args []string) error {
			return inGameRoot(func(root string) error { return run(cmd.OutOrStdout(), root, args) })
		}}
		vcsCmd.AddCommand(c)
		return c
	}
	sub("commit [message]", "Save every change as a version", cobra.MaximumNArgs(1), func(w io.Writer, root string, args []string) error {
		v, err := commitGame(root, strings.Join(args, ""))
		if err == nil {
			fmt.Fprintf(w, "Committed %s: %s (%d files)\n", v.Hash[:8], v.Subject, len(v.Files))
		}
		return err
	})
	log := sub("log", "The versions, newest first", cobra.NoArgs, func(w io.Writer, root string, _ []string) error {
		versions, err := vcs.Log(root, vcsCount)
		if err != nil {
			return err
		}
		if jsonOutput {
			return writeJSON(w, map[string]any{"versions": versions})
		}
		for _, v := range versions {
			fmt.Fprintf(w, "%s  %s  %s (%d files, %s)\n", v.Hash[:8], time.Unix(v.When, 0).Format("2006-01-02 15:04"), v.Subject, len(v.Files), v.Author)
		}
		return nil
	})
	log.Flags().IntVarP(&vcsCount, "number", "n", 20, "how many")
	restore := sub("restore <file|.>...", "Put files back as they were (changes they'd lose go to git's stash)", cobra.MinimumNArgs(1), func(w io.Writer, root string, args []string) error {
		backup, err := vcs.Restore(root, vcsFrom, args)
		if err == nil {
			fmt.Fprintf(w, "Restored %s.\n", strings.Join(args, ", "))
		}
		if backup != "" {
			fmt.Fprintln(w, strings.ToUpper(backup[:1])+backup[1:]+".")
		}
		return err
	})
	restore.Flags().StringVar(&vcsFrom, "from", "", "the version (jm vcs log; default: the last commit)")
}

// commitGame commits the game's changes and keeps a picture of the game
// then, for the history to show.
func commitGame(root, message string) (vcs.Version, error) {
	v, err := vcs.Commit(root, message)
	if err != nil {
		return v, err
	}
	if thumb := jpegOf(projects.Thumb(root), 320); thumb != nil {
		path := versionThumb(root, v.Hash)
		if os.MkdirAll(filepath.Dir(path), 0o755) == nil {
			_ = os.WriteFile(path, thumb, 0o644) // a picture is a nicety: the commit stands without
		}
	}
	return v, nil
}

// versionThumb is where a version's picture is kept (.jm: never committed).
func versionThumb(root, hash string) string {
	return filepath.Join(root, ".jm", "versions", hash+".jpg")
}

// fileChange is a changed file; for a scene, what changed in it per entity.
type fileChange struct {
	Path     string         `json:"path"`
	Status   string         `json:"status"`
	Entities []entityChange `json:"entities,omitempty"`
}

// entityChange is an entity a scene gained, lost, or changed: the
// components (and "name", "prefab"…) that differ.
type entityChange struct {
	Entity string `json:"entity"` // its name
	// Occurrence: which entity of that name, counting in the scene's order, from 2 (none for the first).
	Occurrence int      `json:"occurrence,omitempty"`
	Change     string   `json:"change"` // added, removed, changed
	Changed    []string `json:"changed,omitempty"`
}

// gameChanges is every change since the last commit, sorted by path.
func gameChanges(root string) []fileChange {
	out := []fileChange{}
	for path, status := range vcs.Status(root) {
		c := fileChange{Path: path, Status: status}
		if strings.HasSuffix(path, ".scene.json") {
			now, _ := os.ReadFile(filepath.Join(root, filepath.FromSlash(path)))
			c.Entities = sceneChanges(vcs.Show(root, "HEAD", path), now)
		}
		out = append(out, c)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Path < out[j].Path })
	return out
}

// sceneChanges compares two versions of a scene entity by entity (by name;
// the second of a name is "Name #2"). Either may be nil (no file).
func sceneChanges(before, after []byte) []entityChange {
	was, is := sceneEntities(before), sceneEntities(after)
	var out []entityChange
	for _, key := range unionKeys(was, is) {
		a, inBefore := was[key]
		b, inAfter := is[key]
		switch {
		case !inBefore:
			out = append(out, entityChange{Entity: entityName(key), Occurrence: entityOccurrence(key), Change: "added"})
		case !inAfter:
			out = append(out, entityChange{Entity: entityName(key), Occurrence: entityOccurrence(key), Change: "removed"})
		default:
			var changed []string
			for _, k := range unionKeys(a, b) {
				if !reflect.DeepEqual(a[k], b[k]) {
					changed = append(changed, k)
				}
			}
			if len(changed) > 0 {
				out = append(out, entityChange{Entity: entityName(key), Occurrence: entityOccurrence(key), Change: "changed", Changed: changed})
			}
		}
	}
	return out
}

// sceneEntities is every entity in a scene, children included, by name: each
// one's components (or a prefab instance's overrides) and other keys, flat.
func sceneEntities(data []byte) map[string]map[string]any {
	var doc struct {
		Entities []map[string]any `json:"entities"`
	}
	_ = json.Unmarshal(data, &doc)
	out := map[string]map[string]any{}
	seen := map[string]int{}
	eachEntity(doc.Entities, "entities", func(e map[string]any, where string) {
		name, _ := e["name"].(string)
		if name == "" {
			name = where
		}
		seen[name]++
		key := fmt.Sprintf("%s\x00%04d", name, seen[name]) // a name and which of that name: "Coin #2" may be a name too
		flat := map[string]any{}
		for k, v := range e {
			switch k {
			case "children":
			case "components", "overrides":
				parts, _ := v.(map[string]any)
				for c, value := range parts {
					flat[c] = value
				}
			default:
				flat[k] = v
			}
		}
		out[key] = flat
	})
	return out
}

// entityName and entityOccurrence are a sceneEntities key's parts: the name,
// and which of that name (0 for the first).
func entityName(key string) string {
	return key[:strings.LastIndexByte(key, 0)] // a name may hold NULs: the count is after the last
}

func entityOccurrence(key string) int {
	n := key[strings.LastIndexByte(key, 0)+1:]
	if i, _ := strconv.Atoi(n); i > 1 {
		return i
	}
	return 0
}

// label is an entity as people read it, quoted so a name can't pass for
// another: "Coin", the second Coin "Coin" #2, and one named Coin #2 "Coin #2".
func (e entityChange) label() string {
	if e.Occurrence > 1 {
		return fmt.Sprintf("%s #%d", strconv.Quote(e.Entity), e.Occurrence)
	}
	return strconv.Quote(e.Entity)
}

func unionKeys[V any](a, b map[string]V) []string {
	keys := map[string]bool{}
	for k := range a {
		keys[k] = true
	}
	for k := range b {
		keys[k] = true
	}
	out := make([]string, 0, len(keys))
	for k := range keys {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

func changesText(changes []fileChange) string {
	if len(changes) == 0 {
		return "Nothing changed since the last commit.\n"
	}
	var b strings.Builder
	for _, c := range changes {
		fmt.Fprintf(&b, "%-2s %s\n", c.Status, c.Path)
		for _, e := range c.Entities {
			fmt.Fprintf(&b, "     %s %s", e.Change, e.label())
			if len(e.Changed) > 0 {
				fmt.Fprintf(&b, ": %s", strings.Join(e.Changed, ", "))
			}
			b.WriteString("\n")
		}
	}
	return b.String()
}
