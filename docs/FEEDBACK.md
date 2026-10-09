Reviewed PR #4 (agent-path → master, 25 commits, 125 files, +5932/−270) against 6de3620. Verdict: approve, merge with a merge commit, and fix two things soon after (one correctness hole, one reproducibility mismatch). Nothing blocks.

What I verified
go vet ./... clean; go test ./... passes all 9 packages including the new toolchain and docs tests. CI green on the head (0f843ef), and the release run that produced the v0.0.2 draft (d05bd43) passed its verify job, which runs in a bare ubuntu:24.04 container.
The bare-machine claim holds. With node removed from PATH and a fresh $HOME: jm doctor correctly reports Node and AssemblyScript missing; jm init writes the project plus AGENTS.md/CLAUDE.md; first jm build downloads Node v24.21.0 and AssemblyScript 0.28.20 and completes in 7.8 s; second build 1.8 s; jm test --json passes; jm docs lists seven topics; jm doctor --json reports both as managed. The cache is ~/.jm/toolchains/{node-v24.21.0-linux-x64, assemblyscript-0.28.20} (121 MB + 102 MB).
The embedded cli/internal/docs/md/*.md are byte-identical to docs/*.md, and docs_test.go fails on drift. Good.
The AGENTS.md gotcha "assert((1 + 1) == 2) fails" sounded wrong, so I compiled it with the managed asc 0.28.20 --optimize: it does abort at runtime while assert((x + 1) == 2) passes. True, and worth having in the file.
Fix soon (P2)
Scripts attached at runtime are no longer shipped. cli/cmd/jm/build.go referencedScripts only walks JSON content, then man.Assets drops every other .ts as a "module." But the runtime's spawn overrides allow .set("ScriptComponent", "script", "assets/scripts/x.ts") (world.ts:38), so a script attached only from another script compiles, gets reported as "Checked module", and is missing from build/ at spawn time. No demo does this today, which is why CI is quiet. Cheapest fix: also scan the manifest's .ts sources for string literals ending in .ts (same shape as scriptNameProblems) and count those as attached.

Managed and project compilers disagree. toolchain.go pins asc 0.28.20 / binaryen 131-nightly and its comment says "the demos' package-lock.json has the same"; demos/strike_wing/assets/scripts/package-lock.json resolves 0.28.17 / binaryen 129, and init.go:46 writes ^0.28.17, so anyone who runs npm install gets whatever the newest 0.28.x is and "project asc wins." Goldens then depend on which path a machine took. Make jm init write toolchain.ASCVersion exactly, refresh strike_wing's lock, and add a test that the lock's versions equal ascPackages (you already have the pattern in TestEveryPlatformHasAPinnedNode).

Cache entries are created 0700. toolchain.go:298 uses os.MkdirTemp, which makes the directory drwx------, and that mode survives the rename (confirmed on disk). A JM_TOOLCHAIN_DIR pre-filled by root in a container image, or a shared CI cache, is unreadable to other users. os.Chmod(tmp, 0o755) before the rename.

IDE type-checking breaks on a managed-toolchain project. The generated assets/scripts/tsconfig.json extends assemblyscript/std/assembly.json, which only exists after npm install. jm doesn't care, but a human opening the project (or an agent running tsc) sees every i32 unresolved. Lazy fix inside the existing syncScriptPackages: when the project has no own assemblyscript, link node_modules/assemblyscript to the managed package (junction on Windows).

Nits
EngineDriver.cpp getValue: std::stoul(part) on a long all-digit path segment throws out_of_range out of drive(), killing the run on a bad command. Length-check or catch.
run.go:139 error reads "tried relative to ., ., ., beside jm" when root, build dir and cwd coincide; dedupe the list.
README line 95 still says jm generate script player is "auto-registered"; with this PR an unattached script is checked but not shipped (verify-release.sh had to grow an "attach the script" step for exactly this). Say so there, or have generate script print it.
scriptNameProblems matches spawn("x") inside comments and strings; warning-level, so fine, but expect a false positive report from an agent eventually.
bench/agent-runs/ (100 files, 184 KB, one PNG) is reasonable to keep; just be deliberate that it's a curated record, not a growing log, or it becomes the largest thing in the repo.
Done well
toolchain.go is the right shape: pinned versions with sha256/sha512, filepath.IsLocal on every extracted path, temp-dir-then-rename with the "another jm got there first" case handled, 5 s timeout on node --version, fetch=false errors that say exactly what to run, JM_TOOLCHAIN=managed for reproducible builds, and a test for the checksum-mismatch path. Extracting only bin/node plus LICENSE keeps the download to the executable and stays license-clean.
jm doctor --json is what the MCP and release-verify paths needed, and the engine --version handshake with the warning on mismatch closes the "which engine is this jm talking to" question.
One-pass build diagnostics (errorCount > 0 → finish(false) after content checks) and the did-you-mean on prefab and scene names, reported once per name at both build and run time.
The AGENTS.md template is concrete and every command in it exists; the two fresh-agent runs producing Breakout in ~25 tool calls each is the evidence that matters, and the frictions they hit became fixes in the same PR.
verify-release.sh asserting the container has no node turns the install story into a test.
Process
Merge with a merge commit rather than squash so d05bd43, which the v0.0.2 draft was built from, stays in master's history; otherwise re-run the release workflow from master before publishing, as the PR body already notes. The PR bundles release prep, toolchain, docs restructure and driver features; with CI this green it's acceptable, but items 1 and 2 above are the kind of thing that hides in a 125-file diff, so I'd land them as the first two commits after this one.


-----------

PR #3 (networking, 17 commits, +7732/−391, 189 files). Verdict: mergeable; architecture sound; one design gap to document or close before anyone hosts for strangers. CI green on head (911c106), base 6de3620, mergeable_state clean.

Read fully: Wire.hpp, Transport.hpp, all receive handlers in NetModule.cpp/NetReplication.cpp, host migration, ENet vendoring, server target, CI changes, networking.md. Skimmed demos, CLI, editor.

Good:

Reader is bounds-safe (short read → zeros, ok() false), and every handler checks ok() before acting. Host-only messages (Scene, Group, Bind, Sync, Session, PlayerJoin/Leave) check from.host; entity changes check mayChange (host or owner); Input uses the connection's player, ignores the claimed id; Message sender can't be forged by non-hosts; p2p PeerHello needs the session token; Hello checks protocol, game name and version.
Server = same engine minus frontend libs via link set only; inputs/tilemap split into device/render halves rather than ifdefs. Clean.
ENet pinned (v1.3.18), built from sources, SYSTEM, NOMINMAX handled. One socket kept across sessions so matchmaker addresses stay valid: right call for NAT punching.
Host migration deterministic by construction (_players is std::map, lowest id), no election message.
Interpolation samples bounded (32), per-origin clock offset, angle lerp special-cased.
"Limits" section in docs is honest.
Fix / decide:

Trust boundary wider than docs say. Msg::Spawn from any player is accepted on the host when owner == from.player, for any prefab name, any overrides JSON (including Network/ScriptComponent fields), any controller, no rate limit (NetReplication.cpp:704-730). Docs only say authority: owner "trusts the player"; in practice a modified client in a host-authority game can spawn 10k bosses or an entity with controller = someone else. Cheapest close: host accepts player spawns only for prefabs whose Network says authority: owner (already parsed), plus a per-player spawns-per-second cap; otherwise ignore and log once. Or state plainly in networking.md: no validation, play with friends.
P2P split-brain on migration. onDisconnected → hostLeft() fires when this peer's link to the host drops, even if the host is alive for everyone else (NetModule.cpp:~340). Two hosts, two truths, no detection. Cheap mitigation: before migrating, ask one other peer whether its host link is up, or have the new host announce with a fresh token so a still-alive host's packets get rejected and the partition is at least visible. Minimum: document.
Untrusted sizes uncapped. Reader::str accepts any length up to ENet's default 32 MB packet; Hello.name, Data values, tags get stored and broadcast (amplification). Add a cap in str() (e.g. 64 KiB) and drop oversize packets at handleMessage. Also consider isfinite on f32/f64 wire values before they reach transforms.
Multiplayer demos are excluded from drive/replay and golden checks because NetModule runs on steady_clock (_now = steadySeconds()). That's the honest consequence, but it means the one feature where "same inputs, same state" matters most is the one without it. Not for this PR, but note it: a single-process lockstep session (all peers stepped by one driver, conditions seeded, _now from the fixed clock) would make check-multiplayer.py a determinism test instead of an agreement test.
Scene from host → scenes.requestLoad(path) unvalidated; in folder mode a hostile host can make a client try ../.. paths. Harmless today (JSON parse fails, nothing leaks back), but validateRelativePath-style check costs one line.
Nits:

Spawn with an unknown prefab still creates _tracked/_incoming entries for an invalid entity; guard on the spawner's result.
hello.str() name stored verbatim; strip control chars before it reaches UI/logs.
91 of 189 files are the three demos; fine, but checkers rule tests are the only logic tests among them.
Process: merge commit, not squash (same reason as #4). Land 1 and 3 right after; they're small and they're the difference between "multiplayer for friends" and a server you can put a port forward on.
