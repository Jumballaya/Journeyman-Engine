package archive

import (
	"archive/zip"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
)

func CreateArchive(buildDir, outputPath string) error {
	// Load manifest from build directory
	manifestPath := filepath.Join(buildDir, ".jm.json")
	manifestData, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return fmt.Errorf("failed to load manifest: %w", err)
	}

	// Create zip archive
	archiveFile, err := os.Create(outputPath)
	if err != nil {
		return fmt.Errorf("failed to create archive file: %w", err)
	}
	defer archiveFile.Close()

	zipWriter := zip.NewWriter(archiveFile)
	defer zipWriter.Close()

	// Add manifest
	if err := addFileToZip(zipWriter, manifestPath, ".jm.json"); err != nil {
		return fmt.Errorf("failed to add manifest: %w", err)
	}

	// Add all scenes
	for _, scene := range manifestData.Scenes {
		scenePath := filepath.Join(buildDir, scene)
		if err := addFileToZip(zipWriter, scenePath, scene); err != nil {
			return fmt.Errorf("failed to add scene %s: %w", scene, err)
		}
	}

	// Add all assets
	for _, asset := range manifestData.Assets {
		assetPath := filepath.Join(buildDir, asset)
		
		// Check if it's a directory or file
		info, err := os.Stat(assetPath)
		if err != nil {
			// Asset might not exist, skip with warning
			fmt.Printf("Warning: asset %s not found, skipping\n", asset)
			continue
		}

		if info.IsDir() {
			// Add directory recursively
			if err := addDirectoryToZip(zipWriter, assetPath, asset); err != nil {
				return fmt.Errorf("failed to add asset directory %s: %w", asset, err)
			}
		} else {
			// Add file
			if err := addFileToZip(zipWriter, assetPath, asset); err != nil {
				return fmt.Errorf("failed to add asset %s: %w", asset, err)
			}
		}
	}

	// Create archive manifest for engine
	archiveManifest := map[string]interface{}{
		"name":       manifestData.Name,
		"version":    manifestData.Version,
		"entryScene": manifestData.EntryScene,
		"type":       "archive",
	}

	manifestJSON, err := json.MarshalIndent(archiveManifest, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshal archive manifest: %w", err)
	}

	manifestWriter, err := zipWriter.Create(".jm.json")
	if err != nil {
		return fmt.Errorf("failed to create manifest in archive: %w", err)
	}

	if _, err := manifestWriter.Write(manifestJSON); err != nil {
		return fmt.Errorf("failed to write manifest to archive: %w", err)
	}

	return nil
}

func addFileToZip(zipWriter *zip.Writer, filePath, zipPath string) error {
	file, err := os.Open(filePath)
	if err != nil {
		return err
	}
	defer file.Close()

	writer, err := zipWriter.Create(zipPath)
	if err != nil {
		return err
	}

	_, err = io.Copy(writer, file)
	return err
}

func addDirectoryToZip(zipWriter *zip.Writer, dirPath, zipBasePath string) error {
	return filepath.Walk(dirPath, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		if info.IsDir() {
			return nil
		}

		// Calculate relative path for zip
		relPath, err := filepath.Rel(dirPath, path)
		if err != nil {
			return err
		}

		zipPath := filepath.Join(zipBasePath, relPath)
		zipPath = filepath.ToSlash(zipPath) // Use forward slashes for zip

		return addFileToZip(zipWriter, path, zipPath)
	})
}

func ExtractArchive(archivePath, outputDir string) error {
	reader, err := zip.OpenReader(archivePath)
	if err != nil {
		return fmt.Errorf("failed to open archive: %w", err)
	}
	defer reader.Close()

	for _, file := range reader.File {
		path := filepath.Join(outputDir, file.Name)

		// Create directory if needed
		if file.FileInfo().IsDir() {
			if err := os.MkdirAll(path, os.ModePerm); err != nil {
				return fmt.Errorf("failed to create directory: %w", err)
			}
			continue
		}

		// Create parent directories
		if err := os.MkdirAll(filepath.Dir(path), os.ModePerm); err != nil {
			return fmt.Errorf("failed to create parent directory: %w", err)
		}

		// Extract file
		rc, err := file.Open()
		if err != nil {
			return fmt.Errorf("failed to open file in archive: %w", err)
		}

		outFile, err := os.Create(path)
		if err != nil {
			rc.Close()
			return fmt.Errorf("failed to create output file: %w", err)
		}

		_, err = io.Copy(outFile, rc)
		outFile.Close()
		rc.Close()

		if err != nil {
			return fmt.Errorf("failed to extract file: %w", err)
		}

		// Set file permissions
		if err := os.Chmod(path, file.FileInfo().Mode()); err != nil {
			return fmt.Errorf("failed to set file permissions: %w", err)
		}
	}

	return nil
}

func IsArchive(path string) bool {
	return strings.HasSuffix(strings.ToLower(path), ".jm")
}
