Journeyman {{VERSION}}: early release, anything may change before 1.0.

## Files

Each file is named `journeyman-<what>-<platform>`; platforms are `darwin-arm64` (Apple Silicon), `darwin-amd64` (Intel Mac), `linux-amd64` and `windows-amd64`.

| File | Contents | For |
|---|---|---|
| `journeyman-cli-<platform>.tar.gz` (`.zip` on Windows) | `jm` and the engine | the command line: scripts, CI, AI agents |
| `journeyman-editor-<platform>.zip` (`.tar.gz` on Linux) | the editor, with `jm` and the engine inside | people |
| `journeyman-engine-<platform>` | the engine alone | exporting games for another platform: `jm export --target <platform> --player <file>` |
| `SHA256SUMS` | every file's SHA-256 | checking downloads |

The newest release's files are always at
`https://github.com/Jumballaya/Journeyman-Engine/releases/latest/download/<file>`;
this one's are at `https://github.com/Jumballaya/Journeyman-Engine/releases/download/{{VERSION}}/<file>`.

## Install the CLI

macOS and Linux, one line (installs `jm` and the engine in `~/.jm/bin`, never prompts, prints the `PATH` line to add):

```sh
curl -fsSL https://github.com/Jumballaya/Journeyman-Engine/releases/download/{{VERSION}}/install.sh | sh
```

Or by hand:

```sh
platform=linux-amd64   # or darwin-arm64, darwin-amd64
base=https://github.com/Jumballaya/Journeyman-Engine/releases/download/{{VERSION}}
curl -fsSLO "$base/journeyman-cli-$platform.tar.gz" -O "$base/SHA256SUMS"
sha256sum -c SHA256SUMS --ignore-missing   # macOS: shasum -a 256 -c SHA256SUMS --ignore-missing
tar -xzf "journeyman-cli-$platform.tar.gz"
export PATH="$PWD/journeyman-cli-$platform:$PATH"
jm --version
```

On Windows, unzip `journeyman-cli-windows-amd64.zip` and add the folder to `PATH`.

Nothing else to install: the first `jm build` downloads what compiling scripts needs (Node.js, only if the machine has no 20+, and AssemblyScript), checks each against a pinned checksum and keeps it in `~/.jm/toolchains`. `jm doctor` shows what jm found; `jm doctor --fetch` downloads it ahead of time.

## For AI agents

The CLI pack is all an agent needs, on a machine with nothing else installed:

```sh
jm doctor --json        # versions, script toolchain, what's wrong and how to fix it
jm init "My Game"       # writes AGENTS.md: the edit, build, test, play loop
jm docs                 # the guides (scripting API, formats, testing), built in
```

`jm build --json` reports problems as JSON lines, `JM_DRIVE=1 JM_RENDERER=none jm run` plays the game a step at a time with no display, and `jm mcp` serves the same commands over MCP (`claude mcp add journeyman -- jm mcp`).

## Install the editor

macOS: unzip and move `Journeyman Editor.app` to Applications. Linux and Windows: unzip and run `journeyman_editor`.

## macOS: "damaged" or "unidentified developer"

These builds aren't notarized by Apple, so macOS quarantines them when a browser downloads them (`curl` doesn't). Clear the flag once:

```sh
xattr -dr com.apple.quarantine "/Applications/Journeyman Editor.app"
xattr -dr com.apple.quarantine journeyman-cli-darwin-arm64
```

Games exported on a Mac are ad-hoc signed the same way: players who download them need the same command, or right-click > Open. Windows may show a SmartScreen warning for the same reason: More info > Run anyway.
