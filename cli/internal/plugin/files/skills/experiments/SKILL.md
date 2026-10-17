---
name: experiments
description: Try alternatives without touching the game - variants to compare side by side, sandboxes for your own risky tries. Use when the person says "try a version where", "what if", "A/B", compares two feels, or a change is tuning or a risky refactor.
---

# Variants and sandboxes

Never experiment in the main game. `settings_read` once:
`agent.experiments` says where your risky tries go (sandbox, variant, or ask
the person each time).

**Variants: alternatives the person compares.** Each is a git branch in its
own folder.

1. `variant_new` with a `name` (e.g. `floaty-jump`); `open_game` its dir to
   change it there, `build`, then `vcs_commit` in it.
2. `variant_compare` with `a` (the variant; `b` defaults to the game, `play`
   replays their recorded inputs in both): show them both frames and the
   changes per entity. `variant_play` lets them feel it.
3. They choose: `variant_merge` brings it into the game; `variant_drop`
   throws it away. `variants` lists them.

**Sandboxes: your own tries that may go nowhere** (a refactor, a tuning
sweep, a library).

1. `sandbox_new` copies the game as it is now (`hours` until it expires,
   default 24).
2. Change its files (`open_game` its dir) and `sandbox_run` jm there, e.g.
   `args` `["test", "--json"]`, with a `timeout` in seconds.
3. It worked: `sandbox_promote` with a `variant` name makes it a variant for
   the person to compare. It didn't: `sandbox_drop` (or let it expire).

Afterwards `open_game` the game again; show variants with `studio` and route
`/variants?variant=<name>`.
