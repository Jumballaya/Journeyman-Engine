import assert from "node:assert/strict";
import { test } from "node:test";
import { agentInstallGuide, agentPrompt } from "./agent-install.mjs";

const release = { tag: "v9.9.9", assets: new Set(["install.sh", "install.ps1"]) };
const guide = agentInstallGuide({ release, gh: "https://github.com/x/y", site: "https://x.github.io/y/" });

test("the PowerShell headless check leaves the user's JM_ settings alone", () => {
  const sets = guide.split("\n").filter((line) => line.includes("$env:JM_"));
  assert.ok(sets.length > 0, "the guide has a PowerShell headless check");
  for (const line of sets) assert.match(line, /^powershell -NoProfile -Command \{.*\bjm run \}$/);
  assert.doesNotMatch(guide, /Env:JM_?\*?\s*\|\s*Remove-Item/);
});

test("the copied prompt promises only steps its release's guide has", () => {
  const site = "https://x.github.io/y/";
  const legacy = { tag: "v0.0.4", assets: new Set(["install.sh"]) };
  for (const [rel, has] of [[release, true], [legacy, false], [null, false]]) {
    const prompt = agentPrompt({ release: rel, site });
    const guide = agentInstallGuide({ release: rel, gh: "https://github.com/x/y", site });
    assert.equal(/connect yourself/.test(prompt), has, prompt);
    assert.equal(/jm setup/.test(prompt), /jm setup/.test(guide), prompt);
    assert.equal(/the editor/.test(prompt), /jm editor/.test(guide), prompt);
  }
});

test("the headless check writes only under .jm/", () => {
  const run = guide.split("\n").find((line) => line.startsWith("JM_HEADLESS=1"));
  assert.match(run, /JM_SAVE_DIR=\.jm\/save .*JM_CAPTURE_DIR=\.jm\/frames /);
});

test("the guide's page shows its placeholders", () => {
  // As inline code: rendered to HTML, a bare <this app> is a tag and vanishes.
  assert.match(guide, /Restart `<this app>` and open a new session in\s+`~\/Journeyman\/<Game>`,/);
});
