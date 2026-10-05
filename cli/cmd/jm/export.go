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
	"github.com/Jumballaya/Journeyman-Engine/internal/embed"
	"github.com/Jumballaya/Journeyman-Engine/internal/manifest"
	"github.com/spf13/cobra"
)

var exportOutFlag string
var exportSkipBuildFlag bool
var exportTargetFlag string
var exportPlayerFlag string
var exportBareFlag bool

var exportCmd = &cobra.Command{
	Use:   "export",
	Short: "Build a standalone game: one executable with everything inside",
	Long: `Builds the project and appends its archive to a copy of the engine (the
"player"), giving one self-contained executable:

  macOS:    dist/<Name>.app    (its executable carries the game; --bare for just the binary)
  Linux:    dist/<Name>
  Windows:  dist/<Name>.exe

The result runs without the CLI, Node, the project sources or any data files.

--target os-arch (e.g. linux-amd64, windows-amd64, darwin-arm64) exports for
another platform, using that platform's player: --player <path>, or
players/<target>/journeyman_engine[.exe] next to jm or in $JM_PLAYERS.
Manifest settings under config.export: "icon" (a PNG, macOS) and "bundleId".`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		if !exportSkipBuildFlag {
			buildCmd.Run(buildCmd, nil)
		}
		return runExport(exportOptions{
			buildDir: "build", outDir: exportOutFlag, target: exportTargetFlag,
			player: exportPlayerFlag, bare: exportBareFlag,
		}, cmd.OutOrStdout())
	},
}

func init() {
	exportCmd.Flags().StringVar(&exportOutFlag, "out", "dist", "Output directory")
	exportCmd.Flags().BoolVar(&exportSkipBuildFlag, "skip-build", false, "Export the existing build/ without rebuilding")
	exportCmd.Flags().StringVar(&exportTargetFlag, "target", "", "Platform as os-arch (default: this machine)")
	exportCmd.Flags().StringVar(&exportPlayerFlag, "player", "", "Engine executable for the target platform")
	exportCmd.Flags().BoolVar(&exportBareFlag, "bare", false, "macOS: write the executable alone, not an .app")
}

type exportOptions struct {
	buildDir, outDir, target, player string
	bare                             bool
}

func hostTarget() string { return runtime.GOOS + "-" + runtime.GOARCH }

// findPlayer picks the engine executable the game is appended to.
func findPlayer(opts exportOptions, man manifest.GameManifest, manifestPath string) (string, error) {
	if opts.player != "" {
		if !isFile(opts.player) {
			return "", fmt.Errorf("player not found: %s", opts.player)
		}
		return opts.player, nil
	}
	if opts.target == hostTarget() {
		return resolveEnginePath(man.EnginePath, manifestPath)
	}
	exe := "journeyman_engine"
	if strings.HasPrefix(opts.target, "windows-") {
		exe += ".exe"
	}
	var dirs []string
	if env := os.Getenv("JM_PLAYERS"); env != "" {
		dirs = append(dirs, env)
	}
	if self, err := os.Executable(); err == nil {
		dirs = append(dirs, filepath.Join(filepath.Dir(self), "players"), filepath.Join(filepath.Dir(self), "..", "players"))
	}
	for _, dir := range dirs {
		if candidate := filepath.Join(dir, opts.target, exe); isFile(candidate) {
			return candidate, nil
		}
	}
	return "", fmt.Errorf("no player for %s: build journeyman_engine on that platform (or take it from CI's "+
		"\"players\" artifacts) and pass --player, or put it at players/%s/%s next to jm", opts.target, opts.target, exe)
}

func runExport(opts exportOptions, out io.Writer) error {
	if opts.target == "" {
		opts.target = hostTarget()
	}
	manifestPath := filepath.Join(opts.buildDir, archive.ManifestEntryKey)
	man, err := manifest.LoadManifest(manifestPath)
	if err != nil {
		return fmt.Errorf("export: read %s (run `jm build` first): %w", manifestPath, err)
	}
	playerPath, err := findPlayer(opts, man, manifestPath)
	if err != nil {
		return fmt.Errorf("export: %w", err)
	}
	player, err := os.ReadFile(playerPath)
	if err != nil {
		return fmt.Errorf("export: read player: %w", err)
	}

	tmp, err := os.MkdirTemp("", "jm-export-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	archivePath := filepath.Join(tmp, "game.jm")
	if err := runPack(opts.buildDir, archivePath, false); err != nil {
		return err
	}
	packed, err := os.ReadFile(archivePath)
	if err != nil {
		return err
	}
	game, err := embed.Game(player, packed)
	if err != nil {
		return fmt.Errorf("export: %w", err)
	}

	name := exportName(man.Name)
	macApp := strings.HasPrefix(opts.target, "darwin-") && !opts.bare
	exeName := name
	if strings.HasPrefix(opts.target, "windows-") {
		exeName += ".exe"
	}
	root := filepath.Join(opts.outDir, exeName)
	exeDir := opts.outDir
	if macApp {
		root = filepath.Join(opts.outDir, name+".app")
		exeDir = filepath.Join(root, "Contents", "MacOS")
	}
	if err := os.RemoveAll(root); err != nil {
		return fmt.Errorf("export: clear %s: %w", root, err)
	}
	if err := os.MkdirAll(exeDir, 0o755); err != nil {
		return fmt.Errorf("export: mkdir %s: %w", exeDir, err)
	}
	exePath := filepath.Join(exeDir, exeName)
	if err := os.WriteFile(exePath, game, 0o755); err != nil {
		return fmt.Errorf("export: write %s: %w", exePath, err)
	}

	if macApp {
		resourcesDir := filepath.Join(root, "Contents", "Resources")
		if err := os.MkdirAll(resourcesDir, 0o755); err != nil {
			return err
		}
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
	if embed.IsMachO(player) {
		// Apple Silicon only runs signed code: ad-hoc sign the bundle (or binary).
		if _, err := exec.LookPath("codesign"); err != nil {
			fmt.Fprintf(out, "warning: not signed (codesign needs macOS): run `codesign -s - %s` on a Mac\n", root)
		} else if output, err := exec.Command("codesign", "--force", "--sign", "-", root).CombinedOutput(); err != nil {
			fmt.Fprintf(out, "warning: codesign failed: %v: %s\n", err, output)
		}
	}

	info, _ := os.Stat(exePath)
	fmt.Fprintf(out, "Exported %s (%s, %.1f MB)\n", root, opts.target, float64(info.Size())/(1<<20))
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
