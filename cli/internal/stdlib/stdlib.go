package stdlib

import (
	"embed"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
)

//go:embed runtime/*.ts
var StdLibFiles embed.FS

type StdLib struct {
	PreludeContent string
	TempPath       string
}

func CreateStdLib() (*StdLib, error) {
	tempPath, err := extractStdLibToTemp()
	if err != nil {
		return nil, fmt.Errorf("failed to extract stdlib: %w", err)
	}

	preludeData, err := StdLibFiles.ReadFile("runtime/prelude.ts")
	if err != nil {
		return nil, fmt.Errorf("failed to load prelude.ts: %w", err)
	}

	return &StdLib{
		PreludeContent: string(preludeData),
		TempPath:       tempPath,
	}, nil
}

func extractStdLibToTemp() (string, error) {
	tempDir := filepath.Join(os.TempDir(), "jm_stdlib")
	os.MkdirAll(tempDir, os.ModePerm)

	entries, err := fs.ReadDir(StdLibFiles, "runtime")
	if err != nil {
		return "", fmt.Errorf("failed to list embedded stdlib files: %w", err)
	}

	for _, entry := range entries {
		data, err := StdLibFiles.ReadFile(filepath.Join("runtime", entry.Name()))
		if err != nil {
			return "", fmt.Errorf("failed to read embedded stdlib file: %w", err)
		}

		err = os.WriteFile(filepath.Join(tempDir, entry.Name()), data, 0644)
		if err != nil {
			return "", fmt.Errorf("failed to write stdlib file: %w", err)
		}
	}

	return tempDir, nil
}

func (s *StdLib) MergePrelude(userScript string) string {
	return s.PreludeContent + "\n\n" + userScript
}

func (s *StdLib) Cleanup() {
	os.RemoveAll(s.TempPath)
}
