// The install and first-setup guide written for AI agents. It is published twice: as plain
// markdown at agents/install.md (what the copied prompt points agents at) and as a page.
//
// `release` is the newest published release ({ tag, assets }) or null; commands name its real
// files so an agent never has to guess a URL. `site` is the site's absolute base URL.
export function agentInstallGuide({ release, gh, site }) {
  const tag = release?.tag;
  const dl = tag ? `${gh}/releases/download/${tag}` : `${gh}/releases/latest/download`;
  const hasInstaller = !!release?.assets.has("install.sh");
  const unixInstall = hasInstaller
    ? `curl -fsSL ${dl}/install.sh | sh`
    : `curl -fsSLO ${dl}/journeyman-cli-$PLATFORM.tar.gz
tar -xzf journeyman-cli-$PLATFORM.tar.gz
mkdir -p ~/.jm && rm -rf ~/.jm/bin && mv journeyman-cli-$PLATFORM ~/.jm/bin
rm journeyman-cli-$PLATFORM.tar.gz`;

  return `# Install Journeyman and set up a first game (for AI agents)

You are an AI agent. Your user wants to make a 2D game with you using Journeyman, a small
engine where a game is a folder of plain files that one CLI (\`jm\`) builds, runs and tests.
Follow these steps in order. Run every command yourself; ask your user only where a step
says to. The goal is a working project they can start asking you for features in, with no
setup left for them to do.

Release: ${tag ?? "the newest on " + gh + "/releases"}. Human-readable site: ${site}

## 1. Find the platform

On macOS or Linux run \`uname -s\` and \`uname -m\`. On Windows use PowerShell.

| System | Machine | PLATFORM |
|---|---|---|
| Darwin | arm64 | darwin-arm64 |
| Darwin | x86_64 | darwin-amd64 |
| Linux | x86_64 | linux-amd64 |
| Windows | x64 | windows-amd64 |

Anything else (Linux on ARM, 32-bit Windows) has no prebuilt release. Tell your user, and
offer to build from source: ${gh}#build

## 2. Make sure Node.js 20 or newer is installed

Game scripts are AssemblyScript and compile with Node. Check with \`node --version\`.

If it is missing or older than v20, install the current LTS with the system's package
manager, after telling your user what you are installing:
- macOS: \`brew install node\` (if Homebrew is missing, ask your user to install Node from https://nodejs.org)
- Debian or Ubuntu: \`sudo apt-get install -y nodejs npm\`, and if that gives a version below 20, use https://github.com/nodesource/distributions
- Windows: \`winget install OpenJS.NodeJS.LTS\`, then open a new shell

## 3. Install the CLI (jm and the engine)

If \`jm --version\` already works and prints ${tag ?? "a version"}, skip to step 4.

macOS and Linux (set PLATFORM from step 1):

\`\`\`sh
PLATFORM=darwin-arm64   # from step 1
${unixInstall}
export PATH="$HOME/.jm/bin:$PATH"
\`\`\`

Make PATH permanent: append \`export PATH="$HOME/.jm/bin:$PATH"\` to \`~/.zshrc\` (macOS
default shell) or \`~/.bashrc\` (Linux), unless that line is already there.

Windows (PowerShell):

\`\`\`powershell
Invoke-WebRequest ${dl}/journeyman-cli-windows-amd64.zip -OutFile "$env:TEMP\\jm.zip"
Expand-Archive "$env:TEMP\\jm.zip" "$HOME\\.jm" -Force
$bin = "$HOME\\.jm\\journeyman-cli-windows-amd64"
[Environment]::SetEnvironmentVariable("Path", [Environment]::GetEnvironmentVariable("Path", "User") + ";$bin", "User")
$env:Path += ";$bin"
\`\`\`

Verify with \`jm --version\`. The engine binary (\`journeyman_engine\`) sits next to \`jm\`; \`jm run\`
finds it there. Files fetched with curl or PowerShell are not quarantined by macOS, so no
xattr step is needed.

## 4. Create the project

Ask your user for the game's name if they haven't given one; otherwise use "My Game".
Create it where they keep projects (ask if unsure; default to their home directory).
\`jm init\` sets up the current folder, using its argument as the game's name, so make the
folder first:

\`\`\`sh
mkdir my-game && cd my-game
jm init "My Game"
jm build
\`\`\`

If your shell aliases \`cd\` (zoxide and similar), use \`builtin cd\`. Run every later
command from this folder.

The first \`jm build\` installs the AssemblyScript compiler into the project with npm; it can
take a minute. Everything \`jm\` generates goes to \`build/\`; every other file is a source
file you edit.

## 5. Write the project map

Create \`AGENTS.md\` in the project root (and \`CLAUDE.md\` containing just \`See AGENTS.md.\`
if you are Claude Code), so you and any future session know where things are:

\`\`\`markdown
# This project is a Journeyman game

- Gameplay: assets/scripts/*.ts (AssemblyScript, imports from @jm/runtime).
  API: ${gh}/blob/master/docs/scripting.md
  Gameplay helpers: ${gh}/blob/master/docs/runtime-gameplay.md
  File formats (scenes, prefabs, components, UI, input): ${gh}/blob/master/docs/content.md
- Scenes: scenes/*.scene.json. Prefabs: assets/prefabs/*.prefab.json.
- Screens: assets/ui/*.ui.html and .css. Shaders: assets/shaders/*.frag.
- jm generate script|prefab|scene|ui|shader|bindings <name> scaffolds a file. jm generate list shows all.
- Never edit build/. jm build regenerates it.

## Checking your work
- Build: jm build. Rule tests: jm test (tests/*.spec.ts, no build needed).
- To see the game, write a replay (one "frame down|up Key" line per key event,
  e.g. "60 down ArrowRight"), run it headless from the project root, then open the PNGs:
  JM_HEADLESS=1 JM_SAVE_DIR=.jm-save JM_EXIT_AFTER_FRAMES=600 \\
  JM_INPUT_REPLAY=replay.txt JM_CAPTURE_DIR=frames JM_CAPTURE_FRAMES=120,300,590 jm run
- Runs are deterministic: same build, replay and seed give the same frames.
- Show me the frames when you finish a visual change.
\`\`\`

Add \`frames/\` and \`.jm-save/\` to \`.gitignore\` if the project uses git.

## 6. Check that it runs

Run the game headless for a moment and capture a frame:

\`\`\`sh
JM_HEADLESS=1 JM_SAVE_DIR=.jm-save JM_EXIT_AFTER_FRAMES=90 JM_CAPTURE_DIR=frames JM_CAPTURE_FRAMES=60 jm run
\`\`\`

Windows PowerShell sets the same variables first:

\`\`\`powershell
$env:JM_HEADLESS=1; $env:JM_SAVE_DIR=".jm-save"; $env:JM_EXIT_AFTER_FRAMES=90
$env:JM_CAPTURE_DIR="frames"; $env:JM_CAPTURE_FRAMES=60
jm run
\`\`\`

\`frames/frame_00060.png\` should exist. A new project's scene is empty, so the frame is a
plain background; that is expected. Headless still renders with OpenGL 4.1: on a Linux
server or in a sandbox without a display, install xvfb and prefix the command with
\`xvfb-run -a\`.

If a step fails, read the error, fix what it names, and retry. Common causes: \`jm\` not on
PATH in this shell (re-export PATH), Node older than 20, or no OpenGL display.

## 7. Hand over to your user

Tell your user, briefly:
- what you installed and where (\`~/.jm/bin\`, or \`%USERPROFILE%\\.jm\` on Windows) and the project folder;
- that you can now build features and check them yourself with headless runs;
- that the editor is optional: a desktop app for painting maps and playing the game, on the
  same files (${site}download/?kind=editor). Offer to install it.

Then offer a first change they can see, for example: "a 16 by 16 white square that moves
with the arrow keys", and when they agree, build it, capture frames 30 and 120 with a replay
that holds the right arrow, and show them both frames.

More: the full workflow is at ${site}agents/ and six complete example games are in
${gh}/tree/master/demos (clone the repo to open one).
`;
}

// The hidden prompt behind the "Copy prompt for your agent" buttons.
export const agentPrompt = (site) =>
  `Set up Journeyman Engine so I can make a 2D game with you. Read ${site}agents/install.md and follow it step by step: install the engine and its CLI, create my first project, check that it runs, then tell me what we can try first.`;
