package main

import (
	"bytes"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/jsonfmt"

	"github.com/spf13/cobra"
)

var fmtCheck bool

var fmtCmd = &cobra.Command{
	Use:   "fmt [files...]",
	Short: "Write the project's JSON in the shared layout",
	Long: `Rewrites JSON files in the layout the editor and jm both write: two-space
indent, keys in their order, short arrays of scalars on one line, whole
numbers as integers. A file reads the same whoever wrote it, and a small
change is a small diff.

With no files: .jm.json, scenes, and the .json files under assets/ (not
npm's files, builds, or Tiled's .tmj/.tsj). --check changes nothing and
fails if a file isn't in the layout (for CI).`,
	RunE: func(cmd *cobra.Command, args []string) error {
		files := args
		if len(files) == 0 {
			var err error
			if files, err = projectJSONFiles("."); err != nil {
				return err
			}
		}
		var unformatted []string
		for _, f := range files {
			data, err := os.ReadFile(f)
			if err != nil {
				return err
			}
			formatted, err := jsonfmt.Format(data)
			if err != nil {
				return fmt.Errorf("%s: %w", f, err)
			}
			if bytes.Equal(data, formatted) {
				continue
			}
			unformatted = append(unformatted, f)
			if !fmtCheck {
				if err := os.WriteFile(f, formatted, 0644); err != nil {
					return err
				}
				fmt.Println("Formatted", f)
			}
		}
		if fmtCheck && len(unformatted) > 0 {
			return fmt.Errorf("not in the shared layout (run jm fmt):\n  %s", strings.Join(unformatted, "\n  "))
		}
		return nil
	},
}

func init() {
	fmtCmd.Flags().BoolVar(&fmtCheck, "check", false, "change nothing; fail if a file needs formatting")
}

// npm's and TypeScript's files keep their own tools' layout.
var notOurs = []string{"package.json", "package-lock.json", "tsconfig.json", "asconfig.json"}

// formattable: a JSON file jm and the editor write in the shared layout.
func formattable(path string) bool {
	return strings.HasSuffix(path, ".json") && !slices.Contains(notOurs, filepath.Base(path))
}

// projectJSONFiles lists a project's JSON content: .jm.json, scenes and assets.
func projectJSONFiles(root string) ([]string, error) {
	var files []string
	if _, err := os.Stat(filepath.Join(root, ".jm.json")); err == nil {
		files = append(files, filepath.Join(root, ".jm.json"))
	}
	for _, dir := range []string{"scenes", "assets"} {
		err := filepath.WalkDir(filepath.Join(root, dir), func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				if os.IsNotExist(err) {
					return nil
				}
				return err
			}
			if d.IsDir() {
				if name := d.Name(); name == "node_modules" || name == "build" || strings.HasPrefix(name, ".") {
					return filepath.SkipDir
				}
				return nil
			}
			if formattable(path) {
				files = append(files, path)
			}
			return nil
		})
		if err != nil {
			return nil, err
		}
	}
	return files, nil
}
