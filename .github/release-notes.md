Early release: anything may change before 1.0.

## Which file

| You want | Download |
|---|---|
| The editor (with `jm` and the engine inside) | `journeyman-human-{{VERSION}}-<platform>` |
| The command line only: scripts, CI, AI agents | `journeyman-agent-{{VERSION}}-<platform>` |
| An engine build to export games for another platform (`jm export --target <platform> --player <file>`) | `journeyman-player-{{VERSION}}-<platform>` |

Platforms: `darwin-arm64` (Apple Silicon Macs), `darwin-amd64` (Intel Macs), `linux-amd64`, `windows-amd64`.
`SHA256SUMS` lists every file's checksum: `shasum -a 256 -c SHA256SUMS --ignore-missing`.

## Install

**Command line** (macOS/Linux): unpack, and put the folder on your `PATH`:

```sh
tar -xzf journeyman-agent-{{VERSION}}-linux-amd64.tar.gz
export PATH="$PWD/journeyman-{{VERSION}}-linux-amd64:$PATH"
jm --version
```

On Windows, unzip and add the folder to `PATH`.

**Editor**: macOS, unzip and move `Journeyman Editor.app` to Applications. Linux and Windows: unzip and run `journeyman_editor`.

Building game scripts needs [Node.js](https://nodejs.org) 20 or newer for now.

## macOS: "damaged" or "unidentified developer"

These builds aren't notarized by Apple, so macOS quarantines them when they're downloaded with a browser. Clear the flag once:

```sh
xattr -dr com.apple.quarantine "/Applications/Journeyman Editor.app"
xattr -dr com.apple.quarantine journeyman-{{VERSION}}-darwin-arm64
```

Games exported on a Mac are ad-hoc signed the same way: players who download them need the same command, or right-click > Open.

Windows may show a SmartScreen warning for the same reason: More info > Run anyway.
