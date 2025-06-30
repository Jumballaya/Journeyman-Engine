package docker

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/docker/docker/api/types/container"
	"github.com/testcontainers/testcontainers-go"
	"github.com/testcontainers/testcontainers-go/wait"
)

type DockerBuilder struct {
	Container testcontainers.Container
}

func NewDockerBuilder() (*DockerBuilder, error) {
	ctx := context.Background()

	absProjectRoot, err := filepath.Abs(".")
	if err != nil {
		return nil, fmt.Errorf("could not determine absolute path: %w", err)
	}

	dockerfilePath := filepath.Join(os.TempDir(), "jm-asc-build")
	os.MkdirAll(dockerfilePath, os.ModePerm)

	dockerfileContent := `
FROM node:18-alpine
RUN npm install -g assemblyscript
WORKDIR /src
ENTRYPOINT ["tail", "-f", "/dev/null"]
`
	err = os.WriteFile(filepath.Join(dockerfilePath, "Dockerfile"), []byte(dockerfileContent), 0644)
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

	return &DockerBuilder{Container: ctr}, nil
}

func (b *DockerBuilder) BuildAssemblyScript(sourcePath, outputPath, runtimePath string) error {
	ctx := context.Background()

	cmd := []string{
		"asc", sourcePath,
		"--path", runtimePath,
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

func (b *DockerBuilder) Close() {
	ctx := context.Background()
	b.Container.Terminate(ctx)
}
