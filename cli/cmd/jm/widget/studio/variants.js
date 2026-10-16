// Variants: other takes on the game (jm variant) and the agent's throwaway
// sandboxes (jm sandbox). A variant is compared with the game frame beside
// frame, played, then merged or dropped; a sandbox that worked becomes one.
studioTabs.push((() => {
  "use strict";
  // variants / sandboxes: their tools' answers. picked: { kind: "variant" | "sandbox", name }.
  // compared: { name, frame, result | null } while/after comparing. confirm: the button asked once.
  const fresh = (game) => ({ game, asking: "", seq: 0, variants: null, thumbs: {}, sandboxes: [], picked: null, compared: null,
                             frame: 120, draft: null, newName: "", confirm: "", routed: null });
  let state = fresh(null);

  const said = (r) => ((r && r.content) || []).filter((c) => c.type === "text").map((c) => c.text).join(" ");

  const load = async (ctx) => {
    const n = ++state.seq, mine = state;
    const [v, s] = await Promise.all([ctx.call("variants", {}), ctx.call("sandboxes", {})].map((p) => p.catch(() => null)));
    if (n !== mine.seq || mine !== state) return;
    state.variants = (v && v.structuredContent && v.structuredContent.variants) || [];
    state.thumbs = (v && v._meta && v._meta["jm/thumbs"]) || {};
    state.sandboxes = (s && s.structuredContent && s.structuredContent.sandboxes) || [];
    if (!state.picked && state.variants.length) state.picked = { kind: "variant", name: state.variants[0].name };
    ctx.rerender();
  };

  const run = async (ctx, label, name, args, done, after) => {
    state.confirm = "";
    state.compared = null;  // what it showed may be no longer so
    if (await ctx.act(label, name, args, done) && after) after();
    await load(ctx);
  };

  const compare = async (ctx, name) => {
    const c = state.compared = { name, frame: state.frame, result: null };
    ctx.rerender();
    const r = await ctx.call("variant_compare", { a: name, frame: c.frame }).catch((e) => ({ isError: true, content: [{ type: "text", text: String(e && e.message || e) }] }));
    if (state.compared === c) { c.result = r || { isError: true }; ctx.rerender(); }
  };

  const twice = (ctx, key, label, ask, go) => ctx.kit.el("button", { disabled: ctx.busy, "data-key": "confirm:" + key,
    onclick: () => { if (state.confirm === key) go(); else { state.confirm = key; ctx.rerender(); } } }, state.confirm === key ? ask : label);

  const marks = { M: "changed", A: "added", D: "deleted" };

  const changesList = (el, changes) => el("div", { class: "var-changes" }, changes.map((c) => el("div", {},
    el("span", { class: "var-path" }, c.path), " ", el("span", { class: "meta" }, marks[c.status] || c.status),
    (c.entities || []).map((e) => el("div", { class: "meta var-entity" }, `${e.change} ${e.entity}${e.changed ? ": " + e.changed.join(", ") : ""}`)))));

  const variantView = (ctx, v) => {
    const { el } = ctx.kit, c = state.compared && state.compared.name === v.name ? state.compared : null;
    ctx.share(`The person is looking at variant ${v.name} of ${ctx.game.name} (${v.ahead} commits ahead).`, { variant: v.name, dir: v.dir });
    const frameInput = el("input", { type: "number", min: "1", "aria-label": "Frame", class: "var-frame", "data-key": "var-frame",
      oninput: (e) => { state.frame = Math.max(1, Number(e.target.value) || 1); } });
    frameInput.value = String(state.frame);
    let body;
    if (!c) body = el("div", { class: "meta" }, "Compare runs both builds headless and shows them at the same frame.");
    else if (!c.result) body = el("div", { class: "meta" }, `Comparing at frame ${c.frame}: building and running both…`);
    else if (c.result.isError) body = el("div", { class: "meta" }, said(c.result) || "Couldn't compare.");
    else {
      const s = c.result.structuredContent || {}, frames = (c.result._meta && c.result._meta["jm/frames"]) || [];
      body = el("div", { class: "var-compare" },
        el("div", { class: "var-ab" }, [v.name, "the game"].map((label, i) => el("figure", {},
          frames[i] ? el("img", { src: frames[i], alt: `${label} at frame ${s.frame}` }) : el("div", { class: "var-noframe meta" }, (s.errors || [])[i] || "No frame."),
          el("figcaption", {}, `${label} · frame ${s.frame}`)))),
        (s.changes || []).length ? changesList(el, s.changes) : el("div", { class: "meta" }, "Their commits change no files."));
    }
    return el("section", { class: "section var-detail" },
      el("div", { class: "head" }, el("h1", {}, v.name), el("span", { class: "meta" }, `${v.branch} · ${v.subject || ""}`), el("span", { class: "spacer" }),
        el("button", { disabled: ctx.busy, onclick: () => ctx.act("Starting the variant", "variant_play", { name: v.name }, () => `Variant ${v.name} is open to play.`) }, "Play"),
        twice(ctx, "merge:" + v.name, "Merge", "Merge into the game?", () => run(ctx, "Merging", "variant_merge", { name: v.name }, () => `Variant ${v.name} is merged into the game.`)),
        twice(ctx, "drop:" + v.name, "Drop", v.changed ? `Drop? (${v.changed} uncommitted files go)` : "Drop?", () =>
          run(ctx, "Dropping", "variant_drop", { name: v.name }, (t) => t, () => { state.picked = null; })),
        el("button", { onclick: () => ctx.host.message([ctx.host.text(`About variant ${v.name} of ${ctx.game.name}: `)], { send: false }) }, "Ask about this")),
      el("form", { class: "head", onsubmit: (e) => { e.preventDefault(); compare(ctx, v.name); } },
        el("span", { class: "meta" }, "Against the game at frame"), frameInput,
        el("button", { class: "primary", type: "submit", disabled: !!(c && !c.result) }, "Compare")),
      body);
  };

  const sandboxView = (ctx, s) => {
    const { el } = ctx.kit;
    ctx.share(`The person is looking at sandbox ${s.name} of ${ctx.game.name}.`, { sandbox: s.name, dir: s.dir });
    const changes = Object.entries(s.changes || {}).sort().map(([path, status]) => ({ path, status }));
    const keep = state.draft !== null
      ? el("form", { class: "head", onsubmit: (e) => {
          e.preventDefault();
          const as = (state.draft || "").trim();
          if (as) run(ctx, "Keeping it", "sandbox_promote", { name: s.name, variant: as }, () => `Sandbox ${s.name} is variant ${as} now.`,
            () => { state.picked = { kind: "variant", name: as }; state.draft = null; });
        } }, (() => {
          const input = el("input", { placeholder: "The variant's name", "aria-label": "Variant name", "data-key": "var-keep", class: "var-grow",
            oninput: (e) => { state.draft = e.target.value; } });
          input.value = state.draft;
          return input;
        })(), el("button", { class: "primary", type: "submit", disabled: ctx.busy }, "Keep"),
          el("button", { type: "button", onclick: () => { state.draft = null; ctx.rerender(); } }, "Cancel"))
      : null;
    return el("section", { class: "section var-detail" },
      el("div", { class: "head" }, el("h1", {}, s.name), el("span", { class: "badge" }, "sandbox"),
        el("span", { class: "meta" }, `expires ${new Date(s.expires * 1000).toLocaleString([], { dateStyle: "medium", timeStyle: "short" })}`), el("span", { class: "spacer" }),
        el("button", { disabled: ctx.busy, onclick: () => { state.draft = ""; ctx.rerender(); } }, "Keep as a variant"),
        twice(ctx, "sdrop:" + s.name, "Drop", "Drop?", () => run(ctx, "Dropping", "sandbox_drop", { name: s.name }, (t) => t, () => { state.picked = null; }))),
      keep,
      changes.length ? changesList(el, changes) : el("div", { class: "meta" }, "The same as the game."));
  };

  const styled = (el) => {
    if (styled.done) return;
    styled.done = true;
    document.head.append(el("style", {}, `
      .var-grow { flex: 1; min-width: 140px; } .var-frame { width: 90px; }
      .var-list { display: grid; gap: 2px; }
      .var-row { all: unset; display: grid; grid-template-columns: 72px minmax(0, 1fr); column-gap: 10px; padding: 5px 6px; border-radius: 6px; cursor: pointer; }
      .var-row:hover, .var-row:focus-visible { background: var(--raised); } .var-row.on { background: var(--accent-soft); }
      .var-row img, .var-row .var-nothumb { grid-row: span 2; width: 72px; aspect-ratio: 16 / 9; object-fit: cover; border-radius: 4px; background: var(--raised); image-rendering: pixelated; }
      .var-name { font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
      .var-detail { display: grid; gap: 10px; align-content: start; padding: 10px 12px; }
      .var-ab { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
      @media (max-width: 700px) { .var-ab { grid-template-columns: 1fr; } }
      .var-ab figure { margin: 0; display: grid; gap: 4px; } .var-ab img { width: 100%; border-radius: var(--radius); image-rendering: pixelated; background: #000; }
      .var-ab figcaption { color: var(--muted); font-size: 12px; } .var-noframe { aspect-ratio: 16 / 9; display: grid; place-items: center; background: var(--raised); border-radius: var(--radius); padding: 8px; text-align: center; }
      .var-compare, .var-changes { display: grid; gap: 6px; } .var-path { font: 12px ui-monospace, Menlo, monospace; } .var-entity { padding-left: 14px; }`));
  };

  const row = (ctx, kind, item, thumb, meta) => {
    const { el } = ctx.kit, on = state.picked && state.picked.kind === kind && state.picked.name === item.name;
    return el("button", { class: "var-row" + (on ? " on" : ""), "data-key": `${kind}:${item.name}`, title: item.dir,
      onclick: () => { state.picked = { kind, name: item.name }; state.confirm = ""; state.draft = null; ctx.rerender(); } },
      thumb ? el("img", { src: thumb, alt: "" }) : el("span", { class: "var-nothumb" }), el("span", { class: "var-name" }, item.name), el("span", { class: "meta" }, meta));
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    styled(el);
    if (!ctx.host.can("callTool")) return kit.empty("This host can't run Journeyman's tools here.");
    if (state.game !== game.path) state = fresh(game.path);
    if (state.asking !== String(ctx.visit)) { state.asking = String(ctx.visit); load(ctx); }
    const route = ctx.params.variant || ctx.params.sandbox ? ctx.params : null;  // /variants?variant=floaty
    if (route && state.routed !== route) { state.routed = route; state.picked = ctx.params.variant ? { kind: "variant", name: ctx.params.variant } : { kind: "sandbox", name: ctx.params.sandbox }; }
    if (!state.variants) return kit.empty("Reading the variants…");
    const name = el("input", { placeholder: "New variant, e.g. floaty-jump", "aria-label": "New variant's name", "data-key": "var-new", class: "var-grow",
      oninput: (e) => { state.newName = e.target.value; } });
    name.value = state.newName || "";
    const head = el("form", { class: "head", onsubmit: (e) => {
      e.preventDefault();
      const n = (state.newName || "").trim();
      if (n) run(ctx, "Making the variant", "variant_new", { name: n }, () => `Variant ${n} is made from the last commit.`, () => { state.newName = ""; state.picked = { kind: "variant", name: n }; });
    } }, el("h1", {}, "Variants"), name, el("button", { class: "primary", type: "submit", disabled: ctx.busy }, "New variant"));
    const left = el("div", { class: "pane" },
      kit.section(`Variants · ${state.variants.length}`, state.variants.length ? el("div", { class: "var-list" }, state.variants.map((v) =>
        row(ctx, "variant", v, state.thumbs[v.name], `${v.ahead} ahead${v.behind ? ` · ${v.behind} behind` : ""}${v.changed ? ` · ${v.changed} uncommitted` : ""}`)))
        : el("div", { class: "meta" }, "None yet: a variant tries an idea beside the game, from its last commit.")),
      state.sandboxes.length ? kit.section(`Sandboxes · ${state.sandboxes.length}`, el("div", { class: "var-list" }, state.sandboxes.map((s) =>
        row(ctx, "sandbox", s, null, `${Object.keys(s.changes || {}).length} files differ`)))) : null);
    const p = state.picked;
    const v = p && p.kind === "variant" && state.variants.find((x) => x.name === p.name);
    const s = p && p.kind === "sandbox" && state.sandboxes.find((x) => x.name === p.name);
    const right = v ? variantView(ctx, v) : s ? sandboxView(ctx, s)
      : kit.section("Details", el("div", { class: "meta" }, p ? `No ${p.kind} ${p.name}.` : "Pick a variant to compare it with the game, play it, merge or drop it."));
    return el("div", {}, head, kit.split("variants", left, right, { ratio: 0.34 }));
  };

  return { id: "variants", title: "Variants", order: 80, render };
})());
