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

test("on a Mac the guide leads with the installer when the release has one", () => {
  const withPkg = { tag: "v9.9.9", assets: new Set(["install.sh", "install.ps1", "journeyman-darwin-arm64.pkg"]) };
  const mac = agentInstallGuide({ release: withPkg, gh: "https://github.com/x/y", site: "https://x.github.io/y/" });
  assert.match(mac, /releases\/download\/v9\.9\.9\/journeyman-darwin-\$arch\.pkg/);
  assert.ok(mac.indexOf("open journeyman-darwin-$arch.pkg") < mac.indexOf("install.sh"), "the installer comes before the script");
  assert.doesNotMatch(guide, /\.pkg/);  // a release without one: the script only
});
