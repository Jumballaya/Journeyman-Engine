package main

import (
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/Jumballaya/Journeyman-Engine/internal/archive"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

// The engine looks for this archive next to its executable (or in
// ../Resources inside a macOS bundle) when launched without arguments.
const exportArchiveName = "game.jm"

var exportOutFlag string
var exportSkipBuildFlag bool

var exportCmd = &cobra.Command{
	Use:   "export",
	Short: "Build a standalone, double-clickable game (macOS .app or a folder)",
	Long: `Builds the project, packs it into one archive, and bundles it with the engine:

  macOS:          dist/<Name>.app   (Contents/MacOS/<Name> + Resources/game.jm)
  Linux/Windows:  dist/<Name>/      (<Name>[.exe] + game.jm)

The bundle runs without the CLI, Node, or the project sources. Optional
manifest settings under config.export: "icon" (a PNG path, macOS only) and
"bundleId" (macOS CFBundleIdentifier).`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		if !exportSkipBuildFlag {
			buildCmd.Run(buildCmd, nil)
		}
		return runExport("build", exportOutFlag, cmd.OutOrStdout())
	},
}

func init() {
	exportCmd.Flags().StringVar(&exportOutFlag, "out", "dist", "Output directory")
	exportCmd.Flags().BoolVar(&exportSkipBuildFlag, "skip-build", false, "Export the existing build/ without rebuilding")
}

func runExport(buildDir, outDir string, out io.Writer) error {
	manifestPath := filepath.Join(buildDir, archive.ManifestEntryKey)
	man, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return fmt.Errorf("export: read %s (run `jm build` first): %w", manifestPath, err)
	}
	engine, err := resolveEnginePath(man.EnginePath, manifestPath)
	if err != nil {
		return fmt.Errorf("export: %w", err)
	}

	name := exportName(man.Name)
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return fmt.Errorf("export: mkdir %s: %w", outDir, err)
	}

	var exeDir, resourcesDir, root string
	if runtime.GOOS == "darwin" {
		root = filepath.Join(outDir, name+".app")
		exeDir = filepath.Join(root, "Contents", "MacOS")
		resourcesDir = filepath.Join(root, "Contents", "Resources")
	} else {
		root = filepath.Join(outDir, name)
		exeDir = root
		resourcesDir = root
	}
	if err := os.RemoveAll(root); err != nil {
		return fmt.Errorf("export: clear %s: %w", root, err)
	}
	for _, d := range []string{exeDir, resourcesDir} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			return fmt.Errorf("export: mkdir %s: %w", d, err)
		}
	}

	if err := runPack(buildDir, filepath.Join(resourcesDir, exportArchiveName), false); err != nil {
		return err
	}

	exeName := name
	if runtime.GOOS == "windows" {
		exeName += ".exe"
	}
	exePath := filepath.Join(exeDir, exeName)
	if err := copyFile(engine, exePath); err != nil {
		return fmt.Errorf("export: copy engine: %w", err)
	}
	if err := os.Chmod(exePath, 0o755); err != nil {
		return fmt.Errorf("export: chmod %s: %w", exePath, err)
	}

	if runtime.GOOS == "darwin" {
		iconFile := ""
		if icon := exportConfigString(man, "icon"); icon != "" {
			if made, err := makeIcns(icon, filepath.Join(resourcesDir, "AppIcon.icns")); err != nil {
				fmt.Fprintf(out, "warning: app icon skipped: %v\n", err)
			} else if made {
				iconFile = "AppIcon"
			}
		}
		bundleID := exportConfigString(man, "bundleId")
		if bundleID == "" {
			bundleID = "com.journeyman." + slugify(man.Name)
		}
		plist := infoPlist(name, exeName, bundleID, man.Version, iconFile)
		if err := os.WriteFile(filepath.Join(root, "Contents", "Info.plist"), []byte(plist), 0o644); err != nil {
			return fmt.Errorf("export: write Info.plist: %w", err)
		}
	}

	fmt.Fprintf(out, "Exported %s\n", root)
	if runtime.GOOS == "darwin" {
		fmt.Fprintf(out, "Run it with: open %q\n", root)
	} else {
		fmt.Fprintf(out, "Run it with: %s\n", exePath)
	}
	return nil
}

// exportName keeps the game's display name but drops characters that are
// awkward in file names.
func exportName(name string) string {
	cleaned := strings.Map(func(r rune) rune {
		if strings.ContainsRune(`/\:*?"<>|`, r) {
			return -1
		}
		return r
	}, strings.TrimSpace(name))
	if cleaned == "" {
		return "Game"
	}
	return cleaned
}

func exportConfigString(man manifest.GameManifest, key string) string {
	exp, ok := man.Config["export"].(map[string]interface{})
	if !ok {
		return ""
	}
	v, _ := exp[key].(string)
	return v
}

// makeIcns converts a PNG into an .icns with the macOS `sips` + `iconutil`
// tools. Returns false (no error) when the tools aren't available.
func makeIcns(png, dst string) (bool, error) {
	if _, err := exec.LookPath("iconutil"); err != nil {
		return false, nil
	}
	if _, err := exec.LookPath("sips"); err != nil {
		return false, nil
	}
	tmp, err := os.MkdirTemp("", "jm-icon-")
	if err != nil {
		return false, err
	}
	defer os.RemoveAll(tmp)
	iconset := filepath.Join(tmp, "AppIcon.iconset") // iconutil requires the suffix
	if err := os.Mkdir(iconset, 0o755); err != nil {
		return false, err
	}
	for _, size := range []int{16, 32, 64, 128, 256, 512} {
		for _, scale := range []int{1, 2} {
			px := size * scale
			name := fmt.Sprintf("icon_%dx%d", size, size)
			if scale == 2 {
				name += "@2x"
			}
			cmd := exec.Command("sips", "-z", fmt.Sprint(px), fmt.Sprint(px), png, "--out", filepath.Join(iconset, name+".png"))
			if output, err := cmd.CombinedOutput(); err != nil {
				return false, fmt.Errorf("sips: %v: %s", err, output)
			}
		}
	}
	if output, err := exec.Command("iconutil", "-c", "icns", iconset, "-o", dst).CombinedOutput(); err != nil {
		return false, fmt.Errorf("iconutil: %v: %s", err, output)
	}
	return true, nil
}

func infoPlist(name, exe, bundleID, version, icon string) string {
	if version == "" {
		version = "1.0"
	}
	iconEntry := ""
	if icon != "" {
		iconEntry = fmt.Sprintf("\n  <key>CFBundleIconFile</key><string>%s</string>", icon)
	}
	return fmt.Sprintf(`<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>%[1]s</string>
  <key>CFBundleDisplayName</key><string>%[1]s</string>
  <key>CFBundleExecutable</key><string>%[2]s</string>
  <key>CFBundleIdentifier</key><string>%[3]s</string>
  <key>CFBundleVersion</key><string>%[4]s</string>
  <key>CFBundleShortVersionString</key><string>%[4]s</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSMinimumSystemVersion</key><string>11.0</string>%[5]s
</dict>
</plist>
`, xmlEscape(name), xmlEscape(exe), xmlEscape(bundleID), xmlEscape(version), iconEntry)
}

func xmlEscape(s string) string {
	r := strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;", `"`, "&quot;")
	return r.Replace(s)
}
