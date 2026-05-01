package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

var migrateDryRunFlag bool
var migrateForceFlag bool

var migrateCmd = &cobra.Command{
	Use:   "migrate",
	Short: "Rewrite a Journeyman project to source-only layout (.ts replaces .script.json)",
	Long: `Walks the manifest's assets[] for legacy .script.json entries, rewrites them to the
underlying .ts paths, rewrites scene script references, deletes the .script.json
files, and ensures build/ + *.jm are gitignored.

Idempotent: a second run prints "nothing to migrate" and exits 0.`,
	RunE: func(cmd *cobra.Command, args []string) error {
		return runMigrate(".", os.Stdout, migrateDryRunFlag, migrateForceFlag)
	},
}

func init() {
	migrateCmd.Flags().BoolVar(&migrateDryRunFlag, "dry-run", false, "Print planned changes without writing")
	migrateCmd.Flags().BoolVar(&migrateForceFlag, "force", false, "Skip the clean-git-tree check")
}

type migrateExitCode int

const (
	migrateOK         migrateExitCode = 0
	migrateUserError  migrateExitCode = 1 // missing manifest, broken target, malformed JSON, etc.
	migrateDirtyTree  migrateExitCode = 2 // git tree dirty without --force
)

// migrateError carries an exit code so callers can map errors back to process
// exit codes deterministically.
type migrateError struct {
	code migrateExitCode
	msg  string
}

func (e *migrateError) Error() string { return e.msg }

func newMigrateError(code migrateExitCode, format string, args ...interface{}) error {
	return &migrateError{code: code, msg: fmt.Sprintf(format, args...)}
}

// runMigrate runs the migration in `projectDir`. Output goes to `out` so tests
// can capture stdout. `dryRun=true` prints planned changes without mutating.
// `force=true` skips the git-clean check.
func runMigrate(projectDir string, out io.Writer, dryRun, force bool) error {
	manifestPath := filepath.Join(projectDir, archive.ManifestEntryKey)
	if _, err := os.Stat(manifestPath); err != nil {
		return newMigrateError(migrateUserError,
			"not a Journeyman project root: %s missing", archive.ManifestEntryKey)
	}

	if !force {
		if err := checkCleanGitTree(projectDir, out); err != nil {
			return err
		}
	}

	man, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return newMigrateError(migrateUserError, "parse manifest: %v", err)
	}

	legacyAssets, otherAssets := splitLegacyAssets(man.Assets)

	// Idempotency proof: nothing in assets[] AND no orphaned .script.json on disk.
	orphans, err := findOrphanScriptJsons(projectDir)
	if err != nil {
		return newMigrateError(migrateUserError, "scan for .script.json files: %v", err)
	}
	if len(legacyAssets) == 0 && len(orphans) == 0 {
		fmt.Fprintln(out, "nothing to migrate")
		return nil
	}

	// Build script-json → ts map.
	scriptJsonToTs := map[string]string{}
	for _, sjPath := range legacyAssets {
		key := filepath.ToSlash(filepath.Clean(sjPath))
		fullPath := filepath.Join(projectDir, sjPath)
		data, rerr := os.ReadFile(fullPath)
		if rerr != nil {
			return newMigrateError(migrateUserError, "read %s: %v", sjPath, rerr)
		}
		sa, perr := manifest.LoadScriptAssetFromBytes(data)
		if perr != nil {
			return newMigrateError(migrateUserError, "parse %s: %v", sjPath, perr)
		}
		tsRel := filepath.ToSlash(filepath.Clean(sa.Script))
		tsAbs := filepath.Join(projectDir, sa.Script)
		if _, serr := os.Stat(tsAbs); serr != nil {
			return newMigrateError(migrateUserError,
				"broken target: %s references missing %s", sjPath, sa.Script)
		}
		scriptJsonToTs[key] = tsRel
	}

	// Plan scene rewrites.
	type sceneRewrite struct {
		path     string
		original []byte
		updated  []byte
		changes  []string
	}
	var sceneRewrites []sceneRewrite
	for _, scene := range man.Scenes {
		fullPath := filepath.Join(projectDir, scene)
		original, rerr := os.ReadFile(fullPath)
		if rerr != nil {
			return newMigrateError(migrateUserError, "read scene %s: %v", scene, rerr)
		}
		var sceneJson map[string]interface{}
		if perr := json.Unmarshal(original, &sceneJson); perr != nil {
			return newMigrateError(migrateUserError,
				"malformed scene JSON in %s: %v", scene, perr)
		}
		var changes []string
		rewriteScriptRefs(sceneJson, scriptJsonToTs, &changes)
		updated, _ := json.MarshalIndent(sceneJson, "", "  ")
		if len(changes) > 0 {
			sceneRewrites = append(sceneRewrites, sceneRewrite{
				path: scene, original: original, updated: updated, changes: changes,
			})
		}
	}

	// Plan manifest rewrite — replace .script.json entries with their .ts paths,
	// dedupe (a single .ts may be referenced by multiple .script.json files).
	newAssets := append([]string{}, otherAssets...)
	seen := map[string]bool{}
	for _, sj := range legacyAssets {
		key := filepath.ToSlash(filepath.Clean(sj))
		ts := scriptJsonToTs[key]
		if seen[ts] {
			continue
		}
		seen[ts] = true
		newAssets = append(newAssets, ts)
	}
	sort.Strings(newAssets)

	manifestChanged := !stringSliceEqual(man.Assets, newAssets)

	if dryRun {
		fmt.Fprintln(out, "DRY RUN — no files modified")
		fmt.Fprintln(out)
		for _, r := range sceneRewrites {
			fmt.Fprintf(out, "scene: %s\n", r.path)
			for _, c := range r.changes {
				fmt.Fprintf(out, "  %s\n", c)
			}
		}
		if manifestChanged {
			fmt.Fprintf(out, "manifest %s:\n", archive.ManifestEntryKey)
			for _, a := range legacyAssets {
				fmt.Fprintf(out, "  - %s\n", a)
			}
			for _, a := range newAssets {
				if !contains(otherAssets, a) {
					fmt.Fprintf(out, "  + %s\n", a)
				}
			}
		}
		for _, sj := range legacyAssets {
			fmt.Fprintf(out, "delete: %s\n", sj)
		}
		fmt.Fprintln(out)
		fmt.Fprintf(out, "Would rewrite %d scene(s), update manifest, delete %d .script.json file(s).\n",
			len(sceneRewrites), len(legacyAssets))
		return nil
	}

	// Execute writes in order: scenes → manifest → delete script.jsons → gitignore.
	for _, r := range sceneRewrites {
		if werr := os.WriteFile(filepath.Join(projectDir, r.path), r.updated, 0644); werr != nil {
			return newMigrateError(migrateUserError, "write scene %s: %v", r.path, werr)
		}
	}

	if manifestChanged {
		if werr := writeManifestAssets(manifestPath, newAssets); werr != nil {
			return newMigrateError(migrateUserError, "rewrite manifest: %v", werr)
		}
	}

	for _, sj := range legacyAssets {
		full := filepath.Join(projectDir, sj)
		if rerr := os.Remove(full); rerr != nil && !os.IsNotExist(rerr) {
			return newMigrateError(migrateUserError, "delete %s: %v", sj, rerr)
		}
	}

	gitignoreUpdated, gerr := ensureGitignoreLines(projectDir, []string{"build/", "*.jm"})
	if gerr != nil {
		return newMigrateError(migrateUserError, "update .gitignore: %v", gerr)
	}

	fmt.Fprintf(out, "Migrated:\n")
	fmt.Fprintf(out, "  %d scene(s) rewritten\n", len(sceneRewrites))
	fmt.Fprintf(out, "  %d .script.json file(s) deleted\n", len(legacyAssets))
	if gitignoreUpdated {
		fmt.Fprintln(out, "  .gitignore updated: yes")
	} else {
		fmt.Fprintln(out, "  .gitignore updated: no")
	}
	fmt.Fprintln(out, "Recommended: review with `git diff`, then commit.")
	return nil
}

// splitLegacyAssets partitions assets[] into .script.json entries and everything else.
func splitLegacyAssets(assets []string) (legacy, other []string) {
	for _, a := range assets {
		if strings.HasSuffix(a, ".script.json") {
			legacy = append(legacy, a)
		} else {
			other = append(other, a)
		}
	}
	return
}

// findOrphanScriptJsons walks the project tree (excluding `build/`) for any
// remaining .script.json files. Used for the idempotency check.
func findOrphanScriptJsons(projectDir string) ([]string, error) {
	var found []string
	err := filepath.Walk(projectDir, func(path string, info os.FileInfo, werr error) error {
		if werr != nil {
			return werr
		}
		rel, _ := filepath.Rel(projectDir, path)
		if info.IsDir() {
			if rel == "build" || rel == ".git" {
				return filepath.SkipDir
			}
			return nil
		}
		if strings.HasSuffix(rel, ".script.json") {
			found = append(found, filepath.ToSlash(rel))
		}
		return nil
	})
	return found, err
}

// rewriteScriptRefs descends entities[].components.ScriptComponent.script and
// rewrites .script.json references in-place per the map. Diff lines are
// appended to `changes` for dry-run reporting.
func rewriteScriptRefs(node interface{}, scriptJsonToTs map[string]string, changes *[]string) {
	switch v := node.(type) {
	case map[string]interface{}:
		if comps, ok := v["components"].(map[string]interface{}); ok {
			if sc, ok := comps["ScriptComponent"].(map[string]interface{}); ok {
				if ref, ok := sc["script"].(string); ok && strings.HasSuffix(ref, ".script.json") {
					key := filepath.ToSlash(filepath.Clean(ref))
					if newRef, found := scriptJsonToTs[key]; found {
						sc["script"] = newRef
						*changes = append(*changes, fmt.Sprintf("- %s", ref))
						*changes = append(*changes, fmt.Sprintf("+ %s", newRef))
					} else {
						*changes = append(*changes, fmt.Sprintf("! dangling: %s (not in assets[])", ref))
					}
				}
			}
		}
		for _, child := range v {
			rewriteScriptRefs(child, scriptJsonToTs, changes)
		}
	case []interface{}:
		for _, child := range v {
			rewriteScriptRefs(child, scriptJsonToTs, changes)
		}
	}
}

// writeManifestAssets reads the manifest, replaces the assets[] array, and
// writes it back. Other fields are preserved verbatim from the on-disk file
// (we re-marshal — known cost: whitespace/key-order may shift slightly).
func writeManifestAssets(manifestPath string, newAssets []string) error {
	data, err := os.ReadFile(manifestPath)
	if err != nil {
		return err
	}
	var raw map[string]interface{}
	if err := json.Unmarshal(data, &raw); err != nil {
		return err
	}
	assetIface := make([]interface{}, len(newAssets))
	for i, a := range newAssets {
		assetIface[i] = a
	}
	raw["assets"] = assetIface
	out, err := json.MarshalIndent(raw, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(manifestPath, out, 0644)
}

// ensureGitignoreLines appends each `line` to .gitignore if not already present.
// Returns true if the file was created or modified.
func ensureGitignoreLines(projectDir string, lines []string) (bool, error) {
	gitignorePath := filepath.Join(projectDir, ".gitignore")
	existing, err := os.ReadFile(gitignorePath)
	if err != nil && !os.IsNotExist(err) {
		return false, err
	}
	existingLines := map[string]bool{}
	for _, l := range strings.Split(string(existing), "\n") {
		existingLines[strings.TrimSpace(l)] = true
	}
	var toAppend []string
	for _, l := range lines {
		if !existingLines[l] {
			toAppend = append(toAppend, l)
		}
	}
	if len(toAppend) == 0 {
		return false, nil
	}
	var buf bytes.Buffer
	buf.Write(existing)
	if len(existing) > 0 && !bytes.HasSuffix(existing, []byte("\n")) {
		buf.WriteByte('\n')
	}
	for _, l := range toAppend {
		buf.WriteString(l)
		buf.WriteByte('\n')
	}
	if err := os.WriteFile(gitignorePath, buf.Bytes(), 0644); err != nil {
		return false, err
	}
	return true, nil
}

// checkCleanGitTree refuses migration if `git status --porcelain` reports
// uncommitted changes. Skipped (with warning) if the project is not a git repo.
func checkCleanGitTree(projectDir string, out io.Writer) error {
	if _, err := os.Stat(filepath.Join(projectDir, ".git")); os.IsNotExist(err) {
		fmt.Fprintln(out, "warning: not a git repository — skipping clean-tree check")
		return nil
	}
	cmd := exec.Command("git", "status", "--porcelain")
	cmd.Dir = projectDir
	stdout, err := cmd.Output()
	if err != nil {
		// Treat any git error as "not a usable git repo" and proceed with a
		// warning, mirroring the no-.git case.
		fmt.Fprintf(out, "warning: git status failed (%v) — skipping clean-tree check\n", err)
		return nil
	}
	if scanner := bufio.NewScanner(bytes.NewReader(stdout)); scanner.Scan() {
		return newMigrateError(migrateDirtyTree,
			"uncommitted changes in working tree; commit or pass --force")
	}
	return nil
}

func stringSliceEqual(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func contains(s []string, v string) bool {
	for _, x := range s {
		if x == v {
			return true
		}
	}
	return false
}
