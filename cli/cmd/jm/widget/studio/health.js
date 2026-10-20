// Health: the game's checks (health: doctor, build, tests, goldens, world, art) on a
// board, run on request; each problem says where it is and goes to the chat.
studioTabs.push((() => {
  "use strict";
  const names = ["doctor", "build", "tests", "goldens", "world", "art"];
  const titles = { doctor: "Setup", build: "Build", tests: "Tests", goldens: "Goldens", world: "World", art: "Art" };
  // checks: the last outcome of each, by game path then name; running: names under way.
  const state = { checks: {}, running: new Set(), ctx: null };

  const run = async (ctx, which) => {
    const game = ctx.game.path;
    for (const n of which) state.running.add(n);
    ctx.rerender();
    for (const n of which) {  // one at a time: build before goldens, each shown as it lands
      if (state.ctx.game.path !== game) { state.running.clear(); break; }  // another game opened: its checks aren't this one's
      try {
        const r = await ctx.call("health", { checks: [n] });
        const c = r && r.structuredContent && r.structuredContent.checks && r.structuredContent.checks[0];
        (state.checks[game] = state.checks[game] || {})[n] = c || { name: n, ok: false, summary: "no answer", problems: [] };
      } catch (e) {
        (state.checks[game] = state.checks[game] || {})[n] = { name: n, ok: false, summary: String(e && e.message || e), problems: [] };
      }
      state.running.delete(n);
      ctx.rerender();
    }
  };

  const where = (p) => (p.file ? p.file + (p.line ? `:${p.line}` : "") : "");

  // The art check's sprites, each how far along (planned, drawn, exported, done): one to the chat.
  const sprites = (ctx, assets) => {
    const { el } = ctx.kit;
    return el("div", { class: "tree", style: { marginBottom: "4px" } }, assets.map((a) =>
      el("button", { class: "row", title: "Ask your agent about it", onclick: () => ctx.host.message(`The sprite ${a.title} (${a.note}) is ${a.status}${a.problems ? `, ${a.problems} thing(s) off the art note` : ""}: `, { send: false }) },
        el("span", { class: a.status === "done" ? "badge" : "badge idle" }, a.status),
        el("span", { class: "label" }, a.title, el("span", { class: "meta" }, " · " + (a.sprite || a.note) + (a.problems ? ` · ${a.problems} off` : ""))))));
  };

  const card = (ctx, n, c) => {
    const { kit } = ctx, { el } = kit;
    const running = state.running.has(n);
    const pill = running ? el("span", { class: "badge" }, "running…")
      : !c ? el("span", { class: "badge idle" }, "not run")
      : el("span", { class: "badge" + (c.ok && !c.problems.some((p) => p.level === "error") ? "" : " warn") }, c.ok ? "ok" : "failing");
    return kit.section(titles[n],
      el("div", { class: "head", style: { marginBottom: "4px" } }, pill, el("span", { class: "meta" }, c ? c.summary : ""), el("span", { class: "spacer" }),
        el("button", { disabled: running, onclick: () => run(ctx, [n]) }, c ? "Again" : "Run")),
      c && c.art && c.art.assets && c.art.assets.length ? sprites(ctx, c.art.assets) : null,
      c && c.problems.length ? el("div", { class: "tree" }, c.problems.slice(0, 30).map((p) =>
        el("button", { class: "row", title: "Ask your agent about it", onclick: () => ctx.host.message(`${titles[n]} says ${p.level}${where(p) ? " at " + where(p) : ""}: ${p.message}`, { send: false }) },
          el("span", { class: p.level === "error" ? "badge warn" : "badge" }, p.level),
          el("span", { class: "label", style: { whiteSpace: "normal" } }, p.message, where(p) ? el("span", { class: "meta" }, " · " + where(p)) : null)))) : null);
  };

  const view = (ctx) => {
    const { kit } = ctx, { el } = kit;
    const checks = state.checks[ctx.game.path] || {};
    const done = names.filter((n) => checks[n]);
    if (done.length) {
      ctx.share(`Health of ${ctx.game.name}: ${done.map((n) => `${titles[n]} ${checks[n].summary}`).join("; ")}.`,
        { health: Object.fromEntries(done.map((n) => [n, { ok: checks[n].ok, summary: checks[n].summary, problems: checks[n].problems.slice(0, 10) }])) });
    }
    return el("div", {},
      el("div", { class: "head" }, el("h1", {}, "Health"), el("span", { class: "meta" }, "setup, build, tests, goldens, the design notes and the art: build and goldens take a while"),
        el("span", { class: "spacer" }), el("button", { class: "primary", disabled: state.running.size > 0, onclick: () => run(ctx, names) }, "Check all")),
      el("div", { class: "grid", style: { gridTemplateColumns: "repeat(auto-fit, minmax(200px, 1fr))" } }, names.map((n) => card(ctx, n, checks[n]))));
  };

  // The latest page's ctx, whichever tab: a run sees another game opened.
  return { id: "health", title: "Health", order: 40, render: view, background: (ctx) => { state.ctx = ctx; } };
})());
