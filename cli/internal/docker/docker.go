package docker

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/stdlib"
	"github.com/docker/docker/api/types/container"
	"github.com/testcontainers/testcontainers-go"
	"github.com/testcontainers/testcontainers-go/wait"
)

const fileModeUserRW = 0644

var importRegex = regexp.MustCompile(`<env\.([a-zA-Z0-9_]+)>`)

type DockerBuilder struct {
	Container   testcontainers.Container
	RuntimePath string
}

func NewDockerBuilder(runtimePath string) (*DockerBuilder, error) {
	err := os.MkdirAll(runtimePath, os.ModePerm)
	if err != nil {
		return nil, fmt.Errorf("failed to create build directory: %w", err)
	}

	ctx := context.Background()

	absProjectRoot, err := filepath.Abs(".")
	if err != nil {
		return nil, fmt.Errorf("could not determine absolute path: %w", err)
	}

	dockerfilePath := filepath.Join(os.TempDir(), "jm-asc-build")
	os.MkdirAll(dockerfilePath, os.ModePerm)

	dockerfileContent := `
FROM node:18
RUN npm install -g assemblyscript
RUN apt-get update && apt-get install -y wabt
WORKDIR /src
ENTRYPOINT ["tail", "-f", "/dev/null"]
`

	err = os.WriteFile(filepath.Join(dockerfilePath, "Dockerfile"), []byte(dockerfileContent), fileModeUserRW)
	if err != nil {
		return nil, fmt.Errorf("failed to write Dockerfile: %w", err)
	}

	fromDockerfile := testcontainers.FromDockerfile{
		Context:    dockerfilePath,
		Dockerfile: "Dockerfile",
	}

	req := testcontainers.ContainerRequest{
		FromDockerfile: fromDockerfile,
		Entrypoint:     []string{"tail", "-f", "/dev/null"},
		WaitingFor:     wait.ForLog(""),
		HostConfigModifier: func(hc *container.HostConfig) {
			hc.Binds = append(hc.Binds, fmt.Sprintf("%s:/src", absProjectRoot))
		},
	}

	ctr, err := testcontainers.GenericContainer(ctx, testcontainers.GenericContainerRequest{
		ContainerRequest: req,
		Started:          true,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to start container: %w", err)
	}

	return &DockerBuilder{Container: ctr, RuntimePath: runtimePath}, nil
}

func (b *DockerBuilder) BuildAssemblyScript(mergedSourcePath, outputPath string) error {
	ctx := context.Background()

	cmd := []string{
		"asc", mergedSourcePath,
		"--path", "/src/" + b.RuntimePath,
		"--outFile", outputPath,
	}

	exitCode, reader, err := b.Container.Exec(ctx, cmd)
	if err != nil {
		return fmt.Errorf("error executing asc: %w", err)
	}
	if exitCode != 0 {
		buf := new(strings.Builder)
		if _, copyErr := io.Copy(buf, reader); copyErr != nil {
			return fmt.Errorf("asc failed but couldn't read output: %w", copyErr)
		}
		return fmt.Errorf("asc exited with code %d: %s", exitCode, buf.String())
	}

	return nil
}

func (b *DockerBuilder) BuildScriptMetaData(outputPath string) (stdlib.ScriptMetaData, error) {
	ctx := context.Background()
	cmd := []string{
		"wasm-objdump",
		"-x",
		outputPath,
	}
	exitCode, reader, err := b.Container.Exec(ctx, cmd)
	if err != nil {
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, fmt.Errorf("error executing wasm-objdump: %w", err)
	}
	if exitCode != 0 {
		buf := new(strings.Builder)
		if _, copyErr := io.Copy(buf, reader); copyErr != nil {
			return stdlib.ScriptMetaData{
				Imports: []string{},
				Exposed: []string{},
			}, fmt.Errorf("wasm-objdump failed but couldn't read output: %w", copyErr)
		}
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, fmt.Errorf("wasm-objdump exited with code %d: %s", exitCode, buf.String())
	}

	buf := new(strings.Builder)
	if _, copyErr := io.Copy(buf, reader); copyErr != nil {
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, fmt.Errorf("wasm-objdump: couldn't read output: %w", copyErr)
	}

	imports := []string{}
	scanner := bufio.NewScanner(strings.NewReader(buf.String()))
	for scanner.Scan() {
		line := scanner.Text()
		matches := importRegex.FindStringSubmatch(line)
		if len(matches) == 2 {
			imports = append(imports, matches[1])
		}
	}

	if err := scanner.Err(); err != nil {
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, fmt.Errorf("error scanning wasm-objdump output: %w", err)
	}

	if len(imports) == 0 {
		return stdlib.ScriptMetaData{
			Imports: []string{},
			Exposed: []string{},
		}, nil
	}

	return stdlib.ScriptMetaData{
		Imports: imports,
		Exposed: []string{},
	}, nil
}

func (b *DockerBuilder) Close() {
	ctx := context.Background()
	b.Container.Terminate(ctx)
}
