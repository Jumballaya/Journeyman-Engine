// Index: every file of the game by kind, what each uses and what uses it
// (game_index), a picture of an image, and the files nothing uses.
studioTabs.push((() => {
  "use strict";
  // index: game_index's entries ({ game, visit, entries } | { error }), asked again each visit.
  // picked: a file's path ("/index?path=…" picks it too). previews: pictures by game, visit and path.
  const state = { index: null, picked: null, linked: undefined, query: "", folded: new Set(), previews: {} };

  const load = async (ctx) => {
    const key = { game: ctx.game.path, visit: ctx.visit };
    state.index = { ...key, entries: (state.index && state.index.game === key.game && state.index.entries) || null };
    try {
      const r = await ctx.call("game_index", {});
      const entries = r && r.structuredContent && r.structuredContent.entries;
      if (Array.isArray(entries) && !state.index.entries) {  // long kinds start folded, but the picked file's
        const picked = entries.find((e) => e.path === state.picked);
        for (const k of new Set(entries.map((e) => e.kind))) {
          if (entries.filter((e) => e.kind === k).length > 25 && !(picked && picked.kind === k)) state.folded.add("kind:" + k);
        }
      }
      state.index = Array.isArray(entries) ? { ...key, entries } : { ...key, error: (r && r.content && r.content[0] && r.content[0].text) || "No index." };
    } catch (e) { state.index = { ...key, error: String(e && e.message || e) }; }
    ctx.rerender();
  };

  // A picture of path, asked once per game and visit (the file may have changed).
  const preview = (ctx, path) => {
    const key = `${ctx.game.path}|${ctx.visit}|${path}`;
    if (state.previews[key] !== undefined) return state.previews[key];
    state.previews[key] = null;
    ctx.call("game_index", { path }).then((r) => {
      state.previews[key] = (r && r._meta && r._meta["jm/image"]) || "";
      ctx.rerender();
    }, () => {});
    return null;
  };

  const unused = (e) => !!e.unused;
  const base = (p) => p.split("/").pop();

  const browser = (ctx, entries) => {
    const { kit } = ctx, { el } = kit;
    const q = state.query.toLowerCase();
    const shown = entries.filter((e) => !q || e.path.toLowerCase().includes(q) || (q === "unused" && unused(e)));
    const kinds = [...new Set(shown.map((e) => e.kind))];
    const items = kinds.map((k) => {
      const of = shown.filter((e) => e.kind === k);
      return { id: "kind:" + k, label: k, meta: String(of.length),
        children: of.map((e) => ({ id: e.path, label: base(e.path), title: e.path, meta: [e.detail, unused(e) && "unused"].filter(Boolean).join(" · ") })) };
    });
    return kit.section("Files",
      el("input", { placeholder: "Find a file (\"unused\": what nothing uses)", "aria-label": "Find a file", "data-key": "index-find", value: state.query,
        style: { width: "100%", marginBottom: "6px" }, oninput: (e) => { state.query = e.target.value; ctx.rerender(); } }),
      items.length ? el("div", { style: { maxHeight: "70vh", overflow: "auto" } }, kit.tree({ label: "Files", items, folded: state.folded, selected: state.picked,
        onPick: (item) => { if (!item.id.startsWith("kind:")) pick(ctx, item.id); } })) : el("div", { class: "meta" }, "Nothing matches."));
  };

  // Picks a file, its kind unfolded so it shows in the list.
  const pick = (ctx, path) => {
    state.picked = path;
    const e = state.index && state.index.entries && state.index.entries.find((x) => x.path === path);
    if (e) state.folded.delete("kind:" + e.kind);
    ctx.rerender();
  };

  const links = (ctx, title, paths) => {
    const { kit } = ctx, { el } = kit;
    return el("div", {}, el("h2", { class: "meta", style: { fontSize: "11px", textTransform: "uppercase", margin: "8px 0 2px" } }, `${title} (${paths.length})`),
      paths.length ? kit.tree({ label: title, items: paths.map((p) => ({ id: p, label: p })), onPick: (item) => pick(ctx, item.id) })
        : el("div", { class: "meta" }, title === "Used by" ? "Nothing: is it still needed?" : "Nothing."));
  };

  const details = (ctx, e) => {
    const { kit } = ctx, { el } = kit;
    if (!e) return kit.section("File", el("div", { class: "meta" }, "Pick a file to see what it uses and what uses it."));
    ctx.share(`The person is looking at ${e.path} (${e.kind}) in ${ctx.game.name}: used by ${e.usedBy.length ? e.usedBy.join(", ") : "nothing"}.`,
      { file: e.path, kind: e.kind, uses: e.uses, usedBy: e.usedBy });
    const src = e.kind === "texture" ? preview(ctx, e.path) : null;
    return kit.section("File",
      el("div", { class: "name" }, el("span", {}, base(e.path)), el("span", { class: "badge" }, e.kind), unused(e) ? el("span", { class: "badge warn" }, "unused") : null),
      el("div", { class: "meta" }, e.path + (e.detail ? " · " + e.detail : "")),
      src ? el("img", { src, alt: "", style: { height: "160px", width: "auto", maxWidth: "100%", objectFit: "contain", display: "block", margin: "8px 0", imageRendering: "pixelated",
        background: "repeating-conic-gradient(#8884 0 25%, transparent 0 50%) 0 0 / 16px 16px" } }) : null,
      el("div", { class: "head", style: { margin: "8px 0 0" } },
        e.kind === "scene" ? el("button", { disabled: ctx.busy, onclick: () => ctx.act("Showing it in the editor", "editor_show", { scene: e.path }, () => `${e.path} is open in the editor.`) }, "Show in editor") : null,
        el("button", { onclick: () => ctx.host.message(`About ${e.path}: `, { send: false }) }, "Ask about it")),
      links(ctx, "Uses", e.uses), links(ctx, "Used by", e.usedBy));
  };

  const view = (ctx) => {
    const { kit } = ctx;
    if (ctx.params.path !== state.linked) { state.linked = ctx.params.path; if (state.linked) state.picked = state.linked; }
    if (!state.index || state.index.game !== ctx.game.path || state.index.visit !== ctx.visit) load(ctx);
    const entries = state.index && state.index.entries;
    if (!entries) return kit.empty((state.index && state.index.error) || "Reading the game's files…");
    return kit.split("index", browser(ctx, entries), details(ctx, entries.find((e) => e.path === state.picked)), { ratio: 0.45 });
  };

  return { id: "index", title: "Index", order: 30, render: view };
})());
