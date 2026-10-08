# Agent runs

A fresh AI agent, given nothing but the CLI pack in a bare Linux container,
makes a small game. It writes down every place it stalls (`FRICTION.md`),
and those become fixes. Run one after changes to `jm`, the docs, AGENTS.md or
the driver, and compare with the last.

## Running one

1. `scripts/agent-box.sh /tmp/agent-work` builds this tree's `jm` and engine
   for Linux and starts the container `jmbox`. It has no Node, no display and
   no GPU, and `/work` is `/tmp/agent-work`.
2. Give a new agent (no memory of this repo) the prompt below, with the work
   folder's path filled in.
3. Copy `FRICTION.md`, `PLAYTHROUGH.md` and the game (without `build/` or
   `node_modules/`) into `bench/agent-runs/<date>-<game>/`.

## The prompt

> You are testing a game engine's agent experience as a newcomer. You have
> NEVER seen this engine. Your job: build a small game with it, and write
> down every point of friction.
>
> **Environment (strict rules).** A Linux container named `jmbox` has the
> Journeyman CLI (`jm`) and engine on PATH. Nothing else is installed (no
> Node, no display, no GPU). It has internet for downloads. Run every command
> inside it: `docker exec -w /work/breakout jmbox bash -lc '<command>'`. Its
> /work folder is `<work folder>` on the host: read and write files there
> directly, and view any PNGs you manage to produce. Do not read anything else
> on the host or the web. Everything you learn must come from `jm` itself:
> `jm --help`, `jm docs`, `jm schema`, the AGENTS.md `jm init` writes.
>
> **Task.** In /work/breakout (`jm init "Breakout"`), make a small Breakout:
> a title screen where Enter starts; a paddle moved by the arrow keys; a ball
> bouncing off the walls, the paddle and the bricks; rows of bricks that
> disappear when hit and add to an on-screen score; 3 lives, a game-over
> screen, and Enter back to the title.
>
> **Prove it works without looking:** `jm build` with no errors or warnings;
> `jm test` with a couple of logic tests; a driven headless playthrough
> showing that Enter starts the game, the paddle moves, the score goes up and
> lives go down (transcript in PLAYTHROUGH.md); `jm fmt` at the end.
>
> **Deliverable: /work/FRICTION.md**, kept up to date as you go, most
> damaging first. Each entry gives what happened (the exact command and
> output), what you expected and where you looked, the cost (tool calls), and
> a suggested fix. End with "What worked well". Then report on each feature
> and proof step, and list the top 5 frictions.

## Runs

| Run | Result | What it changed |
|---|---|---|
| [2026-10-08 Breakout](2026-10-08-breakout/) | everything done in ~25 tool calls, 0 blockers; 11 frictions | `state [part] [tag=Name]`; a state's `frame` matches dumps (a recording seemed to replay a frame off); `jm init` says where it wrote; guides written for a game project (engine work moved to development.md); one key-name list; a span's text in UI dumps; `"white"` in draw lists; GLFW failure suggests `JM_RENDERER=none`; only scripts content names are compiled |
