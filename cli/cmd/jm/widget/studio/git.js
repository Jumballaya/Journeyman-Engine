// Git: the game's history (jm vcs). What changed since the last commit (a
// scene's per entity) and its diff, a commit, the versions before with what
// the game looked like, and going back to one (changes lost go to git's stash).
studioTabs.push((() => {
  "use strict";
  // changes / versions: vcs_status's and vcs_log's answers. picked: { kind: "change", path, diff }
  // or { kind: "version", hash }. confirm: the button asked once ("discard:<path>", "back:<hash>", …).
  const fresh = (game) => ({ game, asking: "", seq: 0, changes: null, versions: null, thumbs: {}, error: "", statusError: "", picked: null,
                             message: "", confirm: "", routed: null });
  let state = fresh(null);

  // A call's arguments naming the game shown: another caller opening a game meanwhile can't redirect it.
  const on = (ctx, args = {}) => ({ ...args, game: ctx.game.path });
  const said = (r) => ((r && r.content) || []).filter((c) => c.type === "text").map((c) => c.text).join(" ");

  const load = async (ctx) => {
    const n = ++state.seq, mine = state;
    const [s, l] = await Promise.all([ctx.call("vcs_status", on(ctx)), ctx.call("vcs_log", on(ctx, { count: 200 }))].map((p) => p.catch((e) => ({ isError: true, content: [{ type: "text", text: String(e && e.message || e) }] }))));
    if (n !== mine.seq || mine !== state) return;
    state.changes = (s && s.structuredContent && s.structuredContent.changes) || [];
    state.versions = (l && l.structuredContent && l.structuredContent.versions) || [];
    state.thumbs = (l && l._meta && l._meta["jm/thumbs"]) || {};
    state.error = l && l.isError ? said(l) : "";
    state.statusError = s && s.isError ? said(s) || "Couldn't read what changed." : "";
    if (state.picked && state.picked.kind === "change" && !state.changes.some((c) => c.path === state.picked.path)) state.picked = null;
    if (state.picked && state.picked.kind === "change") return pickChange(ctx, state.picked.path);  // its diff as it is now
    // Something to see from the start: the first change, else the last version.
    if (!state.picked && state.changes.length) return pickChange(ctx, state.changes[0].path);
    if (!state.picked && state.versions.length) state.picked = { kind: "version", hash: state.versions[0].hash };
    ctx.rerender();
  };

  const pickChange = async (ctx, path) => {
    const picked = state.picked = { kind: "change", path, diff: null };
    state.confirm = "";
    ctx.rerender();
    const r = await ctx.call("vcs_diff", on(ctx, { file: path })).catch(() => null);
    if (state.picked !== picked) return;
    const s = r && r.structuredContent;
    picked.diff = s && s.file === path ? s.diff : "Couldn't read the diff.";  // only this file's: the header names it
    ctx.rerender();
  };

  const run = async (ctx, label, name, args, done) => {
    state.confirm = "";
    const mine = state;
    const ok = await ctx.act(label, name, on(ctx, args), done);
    if (mine === state) await load(ctx);  // else another game is shown now: its page reads its own
    return ok;
  };

  // A button that asks once: the first press arms it, the second does it.
  const twice = (ctx, key, label, ask, go) => ctx.kit.el("button", { disabled: ctx.busy, "data-key": "confirm:" + key,
    onclick: () => { if (state.confirm === key) go(); else { state.confirm = key; ctx.rerender(); } } }, state.confirm === key ? ask : label);

  const marks = { M: "changed", A: "added", "?": "new", D: "deleted", U: "conflict" };
  const short = (h) => h.slice(0, 8);

  const changesSection = (ctx) => {
    const kit = ctx.kit, { el } = kit, list = state.changes;
    const input = el("input", { placeholder: "What it does for the player, e.g. Make jumps variable height", "aria-label": "Commit message",
      "data-key": "git-message", class: "git-grow", oninput: (e) => { state.message = e.target.value; } });
    input.value = state.message;
    const commit = el("form", { class: "head", onsubmit: (e) => {
      e.preventDefault();
      const message = state.message.trim(), mine = state;
      run(ctx, "Committing", "vcs_commit", { message }, (t) => { try { return `Committed: ${JSON.parse(t).subject}.`; } catch { return "Committed."; } })
        .then((ok) => { if (ok && mine === state && state.message.trim() === message) { state.message = ""; ctx.rerender(); } });  // failed, or another game's page by now: kept
    } }, input, el("button", { class: "primary", type: "submit", disabled: ctx.busy || (!list.length && state.versions.length > 0) }, state.versions.length ? "Commit" : "Start the history"));
    if (state.statusError) return kit.section("Changes", el("div", { class: "meta" }, state.statusError));
    if (!state.versions.length) return kit.section("Changes", el("div", { class: "meta" }, "No history yet: the first commit keeps the game as it is now."), commit);
    if (!list.length) return kit.section("Changes", el("div", { class: "meta" }, "Nothing changed since the last commit."));
    const items = list.map((c) => ({ id: c.path, label: c.path, meta: marks[c.status] || c.status,
      children: (c.entities || []).map((e, i) => ({ id: c.path + "#" + i, label: `${e.change} ${e.entity}`, meta: (e.changed || []).join(", ") })) }));
    return kit.section(`Changes · ${list.length}`, commit,
      kit.tree({ label: "Changes", items, selected: state.picked && state.picked.kind === "change" ? state.picked.path : null,
        onPick: (item) => pickChange(ctx, item.children ? item.id : item.id.split("#")[0]) }));
  };

  const historySection = (ctx) => {
    const kit = ctx.kit, { el } = kit;
    if (!state.versions.length) return kit.section("History", el("div", { class: "meta" }, state.error || "No commits yet: the first commit starts the history."));
    const rows = state.versions.map((v) => {
      const on = state.picked && state.picked.kind === "version" && state.picked.hash === v.hash;
      return el("button", { class: "git-version" + (on ? " on" : ""), "data-key": "version:" + v.hash, title: v.hash,
        onclick: () => { state.picked = { kind: "version", hash: v.hash }; state.confirm = ""; ctx.rerender(); } },
        state.thumbs[v.hash] ? el("img", { src: state.thumbs[v.hash], alt: "" }) : el("span", { class: "git-nothumb" }),
        el("span", { class: "git-subject" }, v.subject),
        el("span", { class: "meta" }, `${short(v.hash)} · ${kit.ago(v.when)} · ${Object.keys(v.files || {}).length} files`));
    });
    return kit.section(`History · ${state.versions.length}`, el("div", { class: "git-history" }, rows));
  };

  // The diff, its lines colored; headers dimmed.
  const diffView = (el, text) => el("pre", { class: "git-diff" }, text.split("\n").map((l) =>
    el("span", { class: /^(\+\+\+|---|diff |index |new file|deleted file)/.test(l) ? "git-h" : l[0] === "+" ? "git-add" : l[0] === "-" ? "git-del" : l.startsWith("@@") ? "git-at" : "" }, l + "\n")));

  const detail = (ctx) => {
    const kit = ctx.kit, { el } = kit, p = state.picked;
    if (!p) return kit.section("Details", el("div", { class: "meta" }, "Pick a change to see its diff, or a version to see it and go back to it."));
    if (p.kind === "change") {
      const c = state.changes.find((x) => x.path === p.path) || { status: "" };
      ctx.share(`The person is looking at the change to ${p.path} in ${ctx.game.name}.`, { change: p.path, status: c.status, entities: c.entities });
      return el("section", { class: "section git-detail" },
        el("div", { class: "head" }, el("span", { class: "git-path" }, p.path), el("span", { class: "badge" }, marks[c.status] || c.status), el("span", { class: "spacer" }),
          ctx.has("files") && c.status !== "D" ? el("button", { onclick: () => ctx.go(ctx.link("files", { file: p.path })) }, "Look") : null,
          twice(ctx, "discard:" + p.path, "Discard", "Discard? (kept in the stash)", () =>
            run(ctx, "Discarding", "vcs_restore", { files: [p.path] }, () => `${p.path} is back as last committed; your change is in git's stash.`))),
        p.diff === null ? el("div", { class: "meta" }, "Reading the diff…") : diffView(el, p.diff));
    }
    const v = state.versions.find((x) => x.hash === p.hash);
    if (!v) return kit.section("Details", el("div", { class: "meta" }, `No version ${short(p.hash)}.`));
    ctx.share(`The person is looking at version ${short(v.hash)} (“${v.subject}”) of ${ctx.game.name}.`, { version: v.hash, subject: v.subject });
    const files = Object.entries(v.files || {}).sort();
    return el("section", { class: "section git-detail" },
      el("div", { class: "head" }, el("h1", {}, v.subject), el("span", { class: "spacer" }),
        twice(ctx, "back:" + v.hash, "Go back to this", "Go back? (changes kept in the stash)", () =>
          run(ctx, "Going back", "vcs_restore", { files: ["."], from: v.hash }, () => `The game is as it was at ${short(v.hash)}; what changed since is in git's stash.`)),
        el("button", { onclick: () => ctx.host.message([ctx.host.text(`About version ${short(v.hash)} (“${v.subject}”) of ${ctx.game.name}: `)], { send: false }) }, "Ask about this")),
      el("div", { class: "meta" }, `${v.hash} · ${v.author} · ${new Date(v.when * 1000).toLocaleString([], { dateStyle: "medium", timeStyle: "short" })}`),
      state.thumbs[v.hash] ? el("img", { class: "git-shot", src: state.thumbs[v.hash], alt: "The game then" }) : null,
      el("div", { class: "git-files" }, files.map(([path, mark]) => el("div", { class: "git-file" },
        el("span", { class: "git-path" }, path), el("span", { class: "meta" }, marks[mark] || mark),
        mark !== "D" ? twice(ctx, `file:${v.hash}:${path}`, "Restore", "Restore it?", () =>
          run(ctx, "Restoring", "vcs_restore", { files: [path], from: v.hash }, () => `${path} is as it was at ${short(v.hash)}.`)) : null))));
  };

  const styled = (el) => {
    if (styled.done) return;
    styled.done = true;
    document.head.append(el("style", {}, `
      .git-grow { flex: 1; min-width: 160px; }
      .git-history { display: grid; gap: 2px; max-height: 50vh; overflow: auto; }
      .git-version { all: unset; display: grid; grid-template-columns: 64px minmax(0, 1fr); grid-template-rows: auto auto; column-gap: 10px;
        padding: 5px 6px; border-radius: 6px; cursor: pointer; }
      .git-version:hover, .git-version:focus-visible { background: var(--raised); } .git-version.on { background: var(--accent-soft); }
      .git-version img, .git-nothumb { grid-row: span 2; width: 64px; aspect-ratio: 16 / 9; object-fit: cover; border-radius: 4px; background: var(--raised); image-rendering: pixelated; }
      .git-subject { font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
      .git-detail { display: grid; gap: 8px; align-content: start; padding: 10px 12px; }
      .git-path { font: 12px ui-monospace, Menlo, monospace; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
      .git-diff { margin: 0; font: 12px/1.45 ui-monospace, Menlo, monospace; background: var(--bg); border: 1px solid var(--line);
        border-radius: var(--radius); padding: 6px 8px; overflow: auto; max-height: 64vh; white-space: pre; }
      .git-diff span { display: block; } .git-h { color: var(--muted); } .git-at { color: var(--accent); }
      .git-add { background: color-mix(in srgb, #2f9e44 16%, transparent); } .git-del { background: color-mix(in srgb, #e03131 16%, transparent); }
      .git-shot { max-width: min(100%, 480px); max-height: 300px; justify-self: start; border-radius: var(--radius); image-rendering: pixelated; }
      .git-files { display: grid; gap: 2px; } .git-file { display: flex; gap: 8px; align-items: center; min-width: 0; }
      .git-file .git-path { flex: 1; }`));
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    styled(el);
    if (!ctx.host.can("callTool")) return kit.empty("This host can't run Journeyman's tools here.");
    if (state.game !== game.path) state = fresh(game.path);
    if (state.asking !== String(ctx.visit)) { state.asking = String(ctx.visit); load(ctx); }
    const route = ctx.params.version ? ctx.params : null;  // /git?version=<hash>
    if (route && state.routed !== route) { state.routed = route; state.picked = { kind: "version", hash: ctx.params.version }; }
    if (!state.changes) return kit.empty("Reading the history…");
    if (state.picked && state.picked.kind === "version" && state.picked.hash.length < 40) {  // a short hash, as people write them
      const v = state.versions.find((x) => x.hash.startsWith(state.picked.hash));
      if (v) state.picked.hash = v.hash;
    }
    return el("div", {}, kit.split("git", el("div", { class: "pane" }, changesSection(ctx), historySection(ctx)), detail(ctx), { ratio: 0.42 }));
  };

  return { id: "git", title: "Git", order: 70, render };
})());
