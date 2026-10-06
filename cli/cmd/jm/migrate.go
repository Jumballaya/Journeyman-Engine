package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
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

// migrateDirtyTree is jm migrate's exit code for a dirty git tree without --force.
const migrateDirtyTree = 2

// migrateError carries a process exit code other than 1.
type migrateError struct {
	code int
	msg  string
}

func (e *migrateError) Error() string { return e.msg }

// runMigrate runs the migration in projectDir, printing to out. dryRun prints
// the planned changes without writing; force skips the git-clean check.
func runMigrate(projectDir string, out io.Writer, dryRun, force bool) error {
	manifestPath := filepath.Join(projectDir, archive.ManifestEntryKey)
	if _, err := os.Stat(manifestPath); err != nil {
		return fmt.Errorf("not a Journeyman project root: %s missing", archive.ManifestEntryKey)
	}
	if !force {
		if err := checkCleanGitTree(projectDir, out); err != nil {
			return err
		}
	}
	man, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return fmt.Errorf("parse manifest: %v", err)
	}

	legacyAssets, otherAssets := splitLegacyAssets(man.Assets)
	orphans, err := findOrphanScriptJsons(projectDir)
	if err != nil {
		return fmt.Errorf("scan for .script.json files: %v", err)
	}
	if len(legacyAssets) == 0 && len(orphans) == 0 {
		fmt.Fprintln(out, "nothing to migrate")
		return nil
	}

	// Each .script.json's .ts; several may share one, so the new assets dedupe.
	scriptJsonToTs := map[string]string{}
	newAssets := slices.Clone(otherAssets)
	for _, sjPath := range legacyAssets {
		data, err := os.ReadFile(filepath.Join(projectDir, sjPath))
		if err != nil {
			return fmt.Errorf("read %s: %v", sjPath, err)
		}
		sa, err := manifest.LoadScriptAssetFromBytes(data)
		if err != nil {
			return fmt.Errorf("parse %s: %v", sjPath, err)
		}
		if _, err := os.Stat(filepath.Join(projectDir, sa.Script)); err != nil {
			return fmt.Errorf("broken target: %s references missing %s", sjPath, sa.Script)
		}
		ts := filepath.ToSlash(filepath.Clean(sa.Script))
		scriptJsonToTs[filepath.ToSlash(filepath.Clean(sjPath))] = ts
		if !slices.Contains(newAssets, ts) {
			newAssets = append(newAssets, ts)
		}
	}
	sort.Strings(newAssets)
	manifestChanged := !slices.Equal(man.Assets, newAssets)

	type sceneRewrite struct {
		path    string
		updated []byte
		changes []string
	}
	var sceneRewrites []sceneRewrite
	for _, scene := range man.Scenes {
		original, err := os.ReadFile(filepath.Join(projectDir, scene))
		if err != nil {
			return fmt.Errorf("read scene %s: %v", scene, err)
		}
		var sceneJson map[string]any
		if err := json.Unmarshal(original, &sceneJson); err != nil {
			return fmt.Errorf("malformed scene JSON in %s: %v", scene, err)
		}
		var changes []string
		rewriteScriptRefs(sceneJson, scriptJsonToTs, &changes)
		if len(changes) > 0 {
			updated, _ := json.MarshalIndent(sceneJson, "", "  ")
			sceneRewrites = append(sceneRewrites, sceneRewrite{path: scene, updated: updated, changes: changes})
		}
	}

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
				if !slices.Contains(otherAssets, a) {
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

	// Writes go scenes → manifest → delete .script.jsons → gitignore.
	for _, r := range sceneRewrites {
		if err := os.WriteFile(filepath.Join(projectDir, r.path), r.updated, 0644); err != nil {
			return fmt.Errorf("write scene %s: %v", r.path, err)
		}
	}
	if manifestChanged {
		setAssets := func(raw map[string]any) { raw["assets"] = newAssets }
		if err := editManifest(manifestPath, manifestPath, setAssets); err != nil {
			return fmt.Errorf("rewrite manifest: %v", err)
		}
	}
	for _, sj := range legacyAssets {
		if err := os.Remove(filepath.Join(projectDir, sj)); err != nil && !os.IsNotExist(err) {
			return fmt.Errorf("delete %s: %v", sj, err)
		}
	}
	gitignoreUpdated, err := ensureGitignoreLines(projectDir, []string{"build/", "*.jm"})
	if err != nil {
		return fmt.Errorf("update .gitignore: %v", err)
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
func rewriteScriptRefs(node any, scriptJsonToTs map[string]string, changes *[]string) {
	switch v := node.(type) {
	case map[string]any:
		if comps, ok := v["components"].(map[string]any); ok {
			if sc, ok := comps["ScriptComponent"].(map[string]any); ok {
				if ref, ok := sc["script"].(string); ok && strings.HasSuffix(ref, ".script.json") {
					key := filepath.ToSlash(filepath.Clean(ref))
					if newRef, found := scriptJsonToTs[key]; found {
						sc["script"] = newRef
						*changes = append(*changes, "- "+ref, "+ "+newRef)
					} else {
						*changes = append(*changes, fmt.Sprintf("! dangling: %s (not in assets[])", ref))
					}
				}
			}
		}
		for _, child := range v {
			rewriteScriptRefs(child, scriptJsonToTs, changes)
		}
	case []any:
		for _, child := range v {
			rewriteScriptRefs(child, scriptJsonToTs, changes)
		}
	}
}

// ensureGitignoreLines appends each `line` to .gitignore if not already present.
// Returns true if the file was created or modified.
func ensureGitignoreLines(projectDir string, lines []string) (bool, error) {
	gitignorePath := filepath.Join(projectDir, ".gitignore")
	existing, err := os.ReadFile(gitignorePath)
	if err != nil && !os.IsNotExist(err) {
		return false, err
	}
	present := map[string]bool{}
	for _, l := range strings.Split(string(existing), "\n") {
		present[strings.TrimSpace(l)] = true
	}
	var missing strings.Builder
	for _, l := range lines {
		if !present[l] {
			missing.WriteString(l + "\n")
		}
	}
	if missing.Len() == 0 {
		return false, nil
	}
	if len(existing) > 0 && !bytes.HasSuffix(existing, []byte("\n")) {
		existing = append(existing, '\n')
	}
	if err := os.WriteFile(gitignorePath, append(existing, missing.String()...), 0644); err != nil {
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
	if len(bytes.TrimSpace(stdout)) > 0 {
		return &migrateError{migrateDirtyTree, "uncommitted changes in working tree; commit or pass --force"}
	}
	return nil
}
