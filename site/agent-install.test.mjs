import assert from "node:assert/strict";
import { test } from "node:test";
import { agentInstallGuide } from "./agent-install.mjs";

const release = { tag: "v9.9.9", assets: new Set(["install.sh", "install.ps1"]) };
const guide = agentInstallGuide({ release, gh: "https://github.com/x/y", site: "https://x.github.io/y/" });

test("the PowerShell headless check leaves the user's JM_ settings alone", () => {
  const sets = guide.split("\n").filter((line) => line.includes("$env:JM_"));
  assert.ok(sets.length > 0, "the guide has a PowerShell headless check");
  for (const line of sets) assert.match(line, /^powershell -NoProfile -Command \{.*\bjm run \}$/);
  assert.doesNotMatch(guide, /Env:JM_?\*?\s*\|\s*Remove-Item/);
});
