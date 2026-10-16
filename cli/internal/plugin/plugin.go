// Package plugin is Journeyman's plugin for ChatGPT and Codex: the files
// jm setup installs (its skills, the onboarding one first).
package plugin

import "embed"

//go:embed files
var Files embed.FS

// OnboardingSkill is the skill hosts run right after installing the plugin.
const OnboardingSkill = "files/skills/onboarding/SKILL.md"
