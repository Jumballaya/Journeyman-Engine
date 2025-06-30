package stdlib

import (
	"embed"
	"fmt"
	"io/fs"
	"path/filepath"
	"strings"
)

//go:embed runtime/*.ts
var StdLibFiles embed.FS

type StdLib struct {
	PreludeContent string
	LibContent     string
}

func CreateStdLib() (*StdLib, error) {
	preludeData, err := StdLibFiles.ReadFile("runtime/prelude.ts")
	if err != nil {
		return nil, fmt.Errorf("failed to load prelude.ts: %w", err)
	}

	libContent, err := buildLibContent()
	if err != nil {
		return nil, fmt.Errorf("failed to build lib content: %w", err)
	}

	return &StdLib{
		PreludeContent: string(preludeData),
		LibContent:     libContent,
	}, nil
}

func buildLibContent() (string, error) {
	entries, err := fs.ReadDir(StdLibFiles, "runtime")
	if err != nil {
		return "", fmt.Errorf("failed to list embedded stdlib files: %w", err)
	}

	var builder strings.Builder

	for _, entry := range entries {
		if entry.Name() == "prelude.ts" {
			continue
		}

		data, err := StdLibFiles.ReadFile(filepath.Join("runtime", entry.Name()))
		if err != nil {
			return "", fmt.Errorf("failed to read embedded stdlib file: %w", err)
		}

		builder.WriteString(string(data))
		builder.WriteString("\n\n")
	}

	return builder.String(), nil
}

func (s *StdLib) BuildScript(userScript string) string {
	var builder strings.Builder

	builder.WriteString(s.PreludeContent)
	builder.WriteString("\n\n")
	builder.WriteString(s.LibContent)
	builder.WriteString("\n\n")
	builder.WriteString(userScript)

	return builder.String()
}
