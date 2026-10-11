package main

import (
	"errors"
	"fmt"
	"html"
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

var exportFlags exportOptions
var exportSkipBuildFlag bool

var exportCmd = &cobra.Command{
	Use:   "export",
	Short: "Build a standalone game: one app or executable with everything inside",
	Long: `Builds the project and packs it with a copy of the engine (the "player"):

  macOS:    dist/<Name>.app    (the game in Contents/Resources/game.jm; --bare for one binary)
  Linux:    dist/<Name>        (the game appended to the executable)
  Windows:  dist/<Name>.exe

The result runs without the CLI, Node, the project sources or any data files.

macOS exports are signed ad hoc, which runs here but not on other Macs
downloaded from the web. --sign <identity> signs with a Developer ID
Application identity from the keychain ("auto": the only one there; jm doctor
lists them), with the hardened runtime and a timestamp. --notarize <profile>
then sends the .app to Apple with that notarytool keychain profile (make it
once: xcrun notarytool store-credentials <profile>), waits for the verdict
(often minutes) and staples it, so it opens on any Mac. --notarize alone means
--sign auto. jm never sees a password or key: only these keychain names.

--server exports the game's dedicated multiplayer server instead: the same
game files appended to journeyman_server (the engine without its window,
renderer, UI and audio), leaving out images, sounds, UI, shaders and fonts.
It's written as dist/<Name>-server[.exe] and listens on .jm.json's net.port
(JM_NET_PORT overrides). Its scene is net.server.entryScene, if set.

--target os-arch (e.g. linux-amd64, windows-amd64, darwin-arm64) exports for
another platform, using that platform's player: --player <path>, or
players/<target>/journeyman_engine[.exe] next to jm or in $JM_PLAYERS.
Manifest settings under config.export: "icon" (a PNG, macOS) and "bundleId".`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		opts := exportFlags
		opts.buildDir = "build"
		if err := opts.resolve(); err != nil {
			return fmt.Errorf("export: %w", err)
		}
		if !exportSkipBuildFlag {
			buildCmd.Run(buildCmd, nil)
		}
		return runExport(opts, cmd.OutOrStdout())
	},
}

func init() {
	exportCmd.Flags().StringVar(&exportFlags.outDir, "out", "dist", "Output directory")
	exportCmd.Flags().BoolVar(&exportSkipBuildFlag, "skip-build", false, "Export the existing build/ without rebuilding")
	exportCmd.Flags().StringVar(&exportFlags.target, "target", "", "Platform as os-arch (default: this machine)")
	exportCmd.Flags().StringVar(&exportFlags.player, "player", "", "Engine executable for the target platform")
	exportCmd.Flags().BoolVar(&exportFlags.bare, "bare", false, "macOS: write the executable alone, not an .app")
	exportCmd.Flags().BoolVar(&exportFlags.server, "server", false, "Export the dedicated multiplayer server (journeyman_server)")
	exportCmd.Flags().StringVar(&exportFlags.sign, "sign", "", `macOS: sign with this keychain identity ("auto": the one Developer ID)`)
	exportCmd.Flags().StringVar(&exportFlags.notarize, "notarize", "", "macOS: notarize and staple the .app with this notarytool keychain profile")
}

type exportOptions struct {
	buildDir, outDir, target, player string
	bare, server                     bool
	sign, notarize                   string // after resolve, sign is a codesign identity ("-": ad hoc)
}

// resolve fills in the defaults and checks the signing flags, before anything is built.
func (o *exportOptions) resolve() error {
	if o.target == "" {
		o.target = hostTarget()
	}
	mac := strings.HasPrefix(o.target, "darwin-")
	if (o.sign != "" || o.notarize != "") && !mac {
		return fmt.Errorf("--sign and --notarize are for macOS exports, not %s", o.target)
	}
	if o.notarize != "" {
		if !o.macApp() {
			return errors.New("--notarize needs an .app (notarization can't be stapled to a bare binary)")
		}
		if o.sign == "" {
			o.sign = "auto"
		}
	}
	if !mac {
		return nil
	}
	identity, err := resolveIdentity(o.sign)
	o.sign = identity
	return err
}

func (o exportOptions) macApp() bool {
	return strings.HasPrefix(o.target, "darwin-") && !o.bare && !o.server
}

// The executable an export starts from: the game's engine, or the server.
func (o exportOptions) engineName() string {
	if o.server {
		return "journeyman_server"
	}
	return "journeyman_engine"
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
		if opts.server {
			return resolveServerPath()
		}
		return resolveEnginePath()
	}
	exe := opts.engineName()
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
	return "", fmt.Errorf("no player for %s: download %s-%s from a GitHub release "+
		"(or build %s on that platform) and pass --player, or put it at players/%s/%s next to jm",
		opts.target, strings.ReplaceAll(opts.engineName(), "_", "-"), opts.target, opts.engineName(), opts.target, exe)
}

// runExport writes the game for resolved opts.
func runExport(opts exportOptions, out io.Writer) error {
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
	if err := packArchive(opts.buildDir, archivePath, packOptions{server: opts.server}); err != nil {
		return err
	}
	packed, err := os.ReadFile(archivePath)
	if err != nil {
		return err
	}
	macApp := opts.macApp()
	game := player // an .app keeps the game beside its executable, outside what codesign hashes as code
	if !macApp {
		if game, err = embed.Game(player, packed); err != nil {
			return fmt.Errorf("export: %w", err)
		}
	} else if _, err := embed.Find(player); err == nil {
		return errors.New("export: the player already holds a game: export from a bare engine build")
	}

	name := exportName(man.Name)
	if opts.server {
		name += "-server"
	}
	exeName := name
	if strings.HasPrefix(opts.target, "windows-") {
		exeName += ".exe"
	}
	final := filepath.Join(opts.outDir, exeName)
	if macApp {
		final = filepath.Join(opts.outDir, name+".app")
	}
	// Everything is made in a staging folder and swapped in at the end, so a
	// failed export leaves the previous one whole. Same name inside it: codesign
	// knows a bundle by its .app.
	staging := filepath.Join(opts.outDir, ".exporting")
	root := filepath.Join(staging, filepath.Base(final))
	exePath := root // a bare binary is the staged file itself
	if macApp {
		exePath = filepath.Join(root, "Contents", "MacOS", exeName)
	}
	if err := os.RemoveAll(staging); err != nil {
		return fmt.Errorf("export: clear %s: %w", staging, err)
	}
	defer os.RemoveAll(staging)
	if err := os.MkdirAll(filepath.Dir(exePath), 0o755); err != nil {
		return fmt.Errorf("export: mkdir %s: %w", filepath.Dir(exePath), err)
	}
	if err := os.WriteFile(exePath, game, 0o755); err != nil {
		return fmt.Errorf("export: write %s: %w", exePath, err)
	}

	if macApp {
		resourcesDir := filepath.Join(root, "Contents", "Resources")
		if err := os.MkdirAll(resourcesDir, 0o755); err != nil {
			return err
		}
		if err := os.WriteFile(filepath.Join(resourcesDir, "game.jm"), packed, 0o644); err != nil {
			return fmt.Errorf("export: write game.jm: %w", err)
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
	if strings.HasPrefix(opts.target, "darwin-") {
		// Apple Silicon only runs signed code: at least ad hoc.
		if _, err := exec.LookPath("codesign"); err != nil && opts.sign == "-" {
			fmt.Fprintf(out, "warning: not signed (codesign needs macOS): run `codesign -s - %s` on a Mac\n", final)
		} else if err := codesign(root, opts.sign); err != nil {
			return fmt.Errorf("export: %w", err)
		}
		if opts.notarize != "" {
			if err := notarize(root, opts.notarize, out); err != nil {
				return fmt.Errorf("export: %w", err)
			}
		}
	}

	if err := os.RemoveAll(final); err != nil {
		return fmt.Errorf("export: clear %s: %w", final, err)
	}
	if err := os.Rename(root, final); err != nil {
		return fmt.Errorf("export: %w", err)
	}
	fmt.Fprintf(out, "Exported %s (%s, %.1f MB)\n", final, opts.target, float64(len(player)+len(packed))/(1<<20))
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
`, html.EscapeString(name), html.EscapeString(exe), html.EscapeString(bundleID), html.EscapeString(version), iconEntry)
}
