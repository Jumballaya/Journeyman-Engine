package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
)

// macOS signing for exports. jm only ever names things in the keychain (a
// codesign identity, a notarytool profile): passwords and keys stay there.

const developerIDPrefix = "Developer ID Application:"

var identityLine = regexp.MustCompile(`^\s*\d+\) ([0-9A-F]{40}) "(.+)"`)

// A keychain code-signing identity; a renewed certificate keeps the name, so
// only the SHA-1 hash tells two apart (codesign takes either).
type signingIdentity struct {
	Hash string `json:"hash"`
	Name string `json:"name"`
}

// signingIdentities lists the keychain's valid code-signing identities;
// none where `security` isn't (not macOS).
func signingIdentities() []signingIdentity {
	out, err := exec.Command("security", "find-identity", "-v", "-p", "codesigning").Output()
	if err != nil {
		return nil
	}
	var ids []signingIdentity
	for _, line := range strings.Split(string(out), "\n") {
		m := identityLine.FindStringSubmatch(line)
		if m != nil && !slices.ContainsFunc(ids, func(id signingIdentity) bool { return id.Hash == m[1] }) {
			ids = append(ids, signingIdentity{m[1], m[2]})
		}
	}
	return ids
}

// resolveIdentity turns --sign into a codesign identity: "" is ad hoc ("-"),
// "auto" the keychain's one Developer ID Application identity.
func resolveIdentity(flag string) (string, error) {
	switch flag {
	case "":
		return "-", nil
	case "auto":
	default:
		return flag, nil
	}
	var ids []string
	for _, id := range signingIdentities() {
		if strings.HasPrefix(id.Name, developerIDPrefix) {
			ids = append(ids, id.Hash+" "+id.Name)
		}
	}
	switch len(ids) {
	case 1:
		_, name, _ := strings.Cut(ids[0], " ")
		return name, nil
	case 0:
		return "", errors.New("--sign auto: no Developer ID Application identity in the keychain " +
			"(make one in Xcode > Settings > Accounts > Manage Certificates; jm doctor lists what's there)")
	}
	return "", fmt.Errorf("--sign auto: %d Developer ID identities in the keychain, pass one with --sign (its hash if the names match):\n  %s",
		len(ids), strings.Join(ids, "\n  "))
}

// codesign signs an .app or a binary: ad hoc, or with a Developer ID identity
// the way notarization requires (hardened runtime, secure timestamp).
func codesign(path, identity string) error {
	args := []string{"--force", "--sign", identity}
	if identity != "-" {
		args = append(args, "--options", "runtime", "--timestamp")
	}
	if output, err := exec.Command("codesign", append(args, path)...).CombinedOutput(); err != nil {
		return fmt.Errorf("codesign: %v: %s", err, strings.TrimSpace(string(output)))
	}
	return nil
}

// notarize sends a signed .app to Apple with a notarytool keychain profile,
// waits for the verdict and staples the ticket to the app.
func notarize(app, profile string, out io.Writer) error {
	tmp, err := os.MkdirTemp("", "jm-notarize-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	zip := filepath.Join(tmp, strings.TrimSuffix(filepath.Base(app), ".app")+".zip")
	if output, err := exec.Command("ditto", "-c", "-k", "--keepParent", app, zip).CombinedOutput(); err != nil {
		return fmt.Errorf("zip for notarization: %v: %s", err, strings.TrimSpace(string(output)))
	}

	fmt.Fprintf(out, "Notarizing with profile %q (usually a few minutes)...\n", profile)
	output, err := exec.Command("xcrun", "notarytool", "submit", zip, "--keychain-profile", profile,
		"--wait", "--output-format", "json").CombinedOutput()
	var result struct{ ID, Status, Message string }
	if json.Unmarshal(output, &result) != nil || result.Status == "" {
		if err == nil {
			err = errors.New("no verdict")
		}
		return fmt.Errorf("notarytool: %v: %s (make the profile with `xcrun notarytool store-credentials %s`)",
			err, strings.TrimSpace(string(output)), profile)
	}
	if result.Status != "Accepted" {
		return fmt.Errorf("notarization %s: %s; why: xcrun notarytool log %s --keychain-profile %s",
			strings.ToLower(result.Status), result.Message, result.ID, profile)
	}
	if output, err := exec.Command("xcrun", "stapler", "staple", app).CombinedOutput(); err != nil {
		return fmt.Errorf("stapler: %v: %s", err, strings.TrimSpace(string(output)))
	}
	return nil
}
