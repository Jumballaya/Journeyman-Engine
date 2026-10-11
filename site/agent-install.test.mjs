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

test("on a Mac the guide leads with the installer when the release has one", () => {
  const withPkg = { tag: "v9.9.9", assets: new Set(["install.sh", "install.ps1", "journeyman-darwin-arm64.pkg"]) };
  const mac = agentInstallGuide({ release: withPkg, gh: "https://github.com/x/y", site: "https://x.github.io/y/" });
  assert.match(mac, /releases\/download\/v9\.9\.9\/journeyman-darwin-\$arch\.pkg/);
  assert.ok(mac.indexOf("open journeyman-darwin-$arch.pkg") < mac.indexOf("install.sh"), "the installer comes before the script");
  assert.doesNotMatch(guide, /\.pkg/);  // a release without one: the script only
});

test("after the installer, the guide makes the installed jm the one later steps run", () => {
  const withPkg = { tag: "v9.9.9", assets: new Set(["install.sh", "install.ps1", "journeyman-darwin-arm64.pkg"]) };
  const mac = agentInstallGuide({ release: withPkg, gh: "https://github.com/x/y", site: "https://x.github.io/y/" });
  const handoff = mac.slice(mac.indexOf("open journeyman-darwin-$arch.pkg"), mac.indexOf("## 2."));
  assert.match(handoff, /export PATH="\/usr\/local\/bin:\$PATH"\nhash -r\njm --version\n/);
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

test("once connected, the guide moves the agent onto jm's tools", () => {
  const connect = guide.slice(guide.indexOf("## 2."), guide.indexOf("## 3."));
  assert.match(connect, /Don't wait for them: go on with steps 3 and 4/);
  assert.match(connect, /if jm's tools .* are among your tools/s);
  assert.doesNotMatch(connect, /do the rest of this guide with/);
  const check = guide.slice(guide.indexOf("## 4."), guide.indexOf("## 5."));
  assert.match(check, /`drive_start` with `gl: true`.*`drive_frame`/s);
});

test("the restart handoff is the guide's last word to the user", () => {
  const handover = guide.slice(guide.indexOf("## 5."), guide.indexOf("More: the workflow"));
  assert.match(handover.trimEnd(), /always end with:\n"Restart `<this app>`[^"]*continue\."$/);
});
