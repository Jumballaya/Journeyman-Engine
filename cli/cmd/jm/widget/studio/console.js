// Console: the game's log (game_log), followed while the tab is shown; the
// files a line names open in Index, and a problem goes to the chat.
studioTabs.push((() => {
  "use strict";
  // log: the last game_log answer ({ game, log, lines, counts }) or { error }, of game (the one shown).
  // problems: warnings and errors only. gen: bumped when the game or filter changes: an answer asked before is stale.
  // follow: the view keeps to the newest line (until the person scrolls up). held: an answer kept back while
  // the person selects text in box (the lines on the page), shown once they're done.
  const state = { log: null, problems: false, ctx: null, polling: false, follow: true, scrolled: 0, game: null, gen: 0, held: null, box: null };
  const forGame = (ctx) => {
    const game = ctx.game ? ctx.game.path : null;
    if (state.game !== game) Object.assign(state, { game, log: null, held: null, gen: state.gen + 1, follow: true });
  };
  const selection = () => {
    const sel = typeof getSelection === "function" ? getSelection() : null;
    return sel && !sel.isCollapsed && state.box && state.box.contains(sel.anchorNode) && state.box.contains(sel.focusNode) ? sel : null;
  };
  const show = (ctx, log) => {
    const before = JSON.stringify(state.log);
    state.log = log;
    state.held = null;
    if (JSON.stringify(log) !== before) ctx.rerender();
  };

  const poll = async () => {
    const ctx = state.ctx;
    forGame(ctx);
    if (ctx.current === "console" && document.visibilityState !== "hidden" && state.game) {
      const asked = { game: state.game, problems: state.problems, gen: state.gen };
      let log;
      try {
        const r = await ctx.call("game_log", { lines: 400, errors: asked.problems, game: asked.game });
        const s = r && r.structuredContent;
        log = s && Array.isArray(s.lines) ? s : { error: (r && r.content && r.content[0] && r.content[0].text) || "No log." };
        if (s && s.game && s.game !== asked.game) log = null;  // another game's
      } catch (e) { log = { error: String(e && e.message || e) }; }
      const current = asked.gen === state.gen && asked.game === state.game && asked.problems === state.problems;
      if (log && current) {
        if (selection()) state.held = log; else show(ctx, log);
      }
    }
    setTimeout(poll, 2000);
  };

  // A line with the game's files linked: "assets/scripts/player.ts:12" opens it in Index.
  const fileRef = /((?:assets|scenes|tests|ui|design)\/[\w./-]+\.\w+)(?::(\d+))?/g;
  const linked = (ctx, text) => {
    const { el } = ctx.kit;
    const out = [];
    let at = 0;
    for (const m of text.matchAll(fileRef)) {
      out.push(text.slice(at, m.index), el("a", { href: "#", onclick: (e) => { e.preventDefault(); ctx.go(ctx.link("index", { path: m[1] })); } }, m[0]));
      at = m.index + m[0].length;
    }
    out.push(text.slice(at));
    return out;
  };

  const view = (ctx) => {
    const { kit } = ctx, { el } = kit;
    const log = state.log;
    const head = el("div", { class: "head" }, el("h1", {}, "Console"),
      log && log.counts ? el("span", { class: "meta" }, `${log.log} · ${log.counts.error} errors, ${log.counts.warning} warnings`) : null,
      el("span", { class: "spacer" }),
      el("label", {}, el("input", { type: "checkbox", checked: state.problems,
        onchange: (e) => { state.problems = e.target.checked; state.log = null; state.held = null; state.gen++; ctx.rerender(); } }), " Warnings and errors only"));
    if (!log) return el("div", {}, head, kit.empty("Reading the log…"));
    if (!log.lines) return el("div", {}, head, kit.empty(log.error));
    if (!log.lines.length) return el("div", {}, head, kit.empty("No log yet: play the game, or run its tests."));
    const sel = selection();
    if (sel) {  // the same lines, the person's selection in them put back once they're on the page again
      const [a, ao, f, fo] = [sel.anchorNode, sel.anchorOffset, sel.focusNode, sel.focusOffset];
      queueMicrotask(() => { const now = getSelection(); if (now && now.setBaseAndExtent) now.setBaseAndExtent(a, ao, f, fo); });
      return el("div", {}, head, state.box);
    }
    const color = { error: "var(--warn)", warning: "var(--accent)" };
    const box = el("div", { class: "section", style: { font: "12px/1.5 var(--font-mono, ui-monospace, monospace)", maxHeight: "75vh", overflow: "auto" },
      onscroll: (e) => { const b = e.target; state.follow = b.scrollTop + b.clientHeight >= b.scrollHeight - 8; state.scrolled = b.scrollTop; } },
        log.lines.map((l) => el("div", { style: { display: "flex", gap: "8px", color: color[l.level] || "inherit", padding: "1px 0" } },
          el("span", { class: "meta", style: { flex: "none" } }, (l.time || "").slice(11, 19)),
          el("span", { style: { flex: "1", minWidth: "0", whiteSpace: "pre-wrap", wordBreak: "break-word" } }, linked(ctx, l.text)),
          color[l.level] ? el("button", { title: "Ask your agent about it", style: { flex: "none", padding: "0 6px" },
            onclick: () => ctx.host.message(`The game logged a${l.level === "error" ? "n" : ""} ${l.level}: ${l.text}`, { send: false }) }, "Ask") : null)));
    queueMicrotask(() => { box.scrollTop = state.follow ? box.scrollHeight : state.scrolled; });  // once it's on the page
    state.box = box;
    return el("div", {}, head, box);
  };

  const background = (ctx) => {
    state.ctx = ctx;
    forGame(ctx);
    if (!state.polling && ctx.host.can("callTool")) {
      state.polling = true;
      document.addEventListener("selectionchange", () => { if (state.held && !selection()) show(state.ctx, state.held); });
      poll();
    }
  };

  return { id: "console", title: "Console", order: 45, render: view, background };
})());
