// Memory: the game's design notes (design/, jm memory): read one with its
// [[links]] and game files followed, note something in it, record a decision.
// Writing at length is for Obsidian or the code editor, which this opens.
studioTabs.push((() => {
  "use strict";
  // listed: the memory answer ({ path, notes } or { path, error }), asked each visit.
  // read: { title, note: memory's answer for it | null }. found: a search's { text, hits }.
  // form: "note" | "decide" while that form is open; draft: its fields.
  // game: whose these are (another game's start over). seq: the latest list asked (an older answer is dropped).
  const fresh = (game) => ({ game, listed: null, asking: "", read: null, found: null, query: "", form: null, draft: {}, routed: "", seq: 0 });
  let state = fresh(null);

  const said = (r) => ((r && r.content) || []).filter((c) => c.type === "text").map((c) => c.text).join(" ");

  const list = async (ctx, path) => {
    const n = ++state.seq, mine = state;
    try {
      const r = await ctx.call("memory", {});
      if (n !== mine.seq || mine !== state) return;
      const s = r && r.structuredContent;
      state.listed = { path, notes: s && Array.isArray(s.notes) ? s.notes : [], error: r && r.isError ? said(r) : "" };
    } catch (e) { if (n !== mine.seq || mine !== state) return; state.listed = { path, notes: [], error: String(e && e.message || e) }; }
    if (!state.read && state.listed.notes.length) open(ctx, (state.listed.notes.find((n) => n.title === "Design") || state.listed.notes[0]).title);
    ctx.rerender();
  };

  const open = async (ctx, title) => {
    const read = state.read = { title, note: null };
    state.form = null;
    ctx.rerender();
    const r = await ctx.call("memory", { note: title }).catch(() => null);
    if (state.read === read) {
      read.note = r && !r.isError && r.structuredContent ? r.structuredContent : { error: said(r) || `No note ${title}.` };
      ctx.rerender();
    }
  };

  const search = async (ctx, text) => {
    state.query = text;
    if (!text.trim()) { state.found = null; ctx.rerender(); return; }
    const r = await ctx.call("memory_search", { text });
    if (state.query === text) { state.found = { text, hits: (r && r.structuredContent && r.structuredContent.hits) || [] }; ctx.rerender(); }
  };

  // [[Note]] and `game/file.json` in a line, as links: a note opens here, a file in Files (a scene in Scene).
  const inline = (ctx, text) => {
    const { el } = ctx.kit, out = [];
    const re = /\[\[([^\]|#]+)(?:[#|]([^\]]*))?\]\]|`([^`]+)`/g;
    let at = 0, m;
    while ((m = re.exec(text))) {
      out.push(text.slice(at, m.index));
      const target = m[1] && m[1].trim();
      if (target) out.push(el("button", { class: "mem-link", onclick: () => open(ctx, target) }, m[2] || target));
      else if (/\.\w+$/.test(m[3]) && /^[\w./ -]+$/.test(m[3])) {
        const file = m[3], scene = /\.scene\.json$/.test(file) && ctx.has("scene");
        out.push(el("button", { class: "mem-link mem-file", title: file, onclick: () => ctx.go(scene ? ctx.link("scene", { scene: file }) : ctx.link("files", { file })) }, file));
      } else out.push(el("code", {}, m[3]));
      at = re.lastIndex;
    }
    out.push(text.slice(at));
    return out;
  };

  // The note's Markdown, as much of it as notes use: headings, observations, relations, lists, paragraphs.
  const body = (ctx, text) => {
    const { el } = ctx.kit;
    const lines = text.replace(/\r\n/g, "\n").replace(/^---\n[\s\S]*?\n---\n/, "").split("\n");
    const out = [];
    let listEl = null;
    for (const line of lines) {
      const obs = /^\s*- \[([^\]]+)\]\s+(.*)$/.exec(line), item = /^\s*- (.*)$/.exec(line), head = /^(#{1,6})\s+(.*)$/.exec(line);
      if (obs || item) {
        if (!listEl) out.push(listEl = el("ul", { class: "mem-list" }));
        listEl.append(obs ? el("li", {}, el("span", { class: "badge" }, obs[1]), " ", inline(ctx, obs[2])) : el("li", {}, inline(ctx, item[1])));
        continue;
      }
      listEl = null;
      if (head) { if (head[1].length > 1) out.push(el("h3", { class: "mem-h" }, head[2])); }
      else if (line.trim()) out.push(el("p", {}, inline(ctx, line)));
    }
    return el("div", { class: "mem-body" }, out);
  };

  const field = (ctx, key, placeholder) => {
    const input = ctx.kit.el("input", { placeholder, "aria-label": placeholder, "data-key": "mem-" + key, class: "mem-grow",
      oninput: (e) => { state.draft[key] = e.target.value; },
      onkeydown: (e) => { if (e.key === "Escape") { state.form = null; ctx.rerender(); } } });
    input.value = state.draft[key] || "";
    return input;
  };

  const save = async (ctx, label, name, args, done, reopen) => {
    if (await ctx.act(label, name, args, done)) { state.form = null; state.draft = {}; }
    await list(ctx, ctx.game.path);
    open(ctx, reopen);
  };

  const forms = (ctx, title) => {
    const { el } = ctx.kit, d = state.draft;
    const buttons = [el("button", { class: "primary", type: "submit", disabled: ctx.busy }, state.form === "note" ? "Note it" : "Record"),
      el("button", { type: "button", onclick: () => { state.form = null; ctx.rerender(); } }, "Cancel")];
    if (state.form === "note") {
      return el("form", { class: "head", onsubmit: (e) => {
        e.preventDefault();
        if ((d.text || "").trim()) save(ctx, "Noting", "memory_note", { note: title, text: d.text.trim(), category: (d.category || "").trim() }, () => `Noted in ${title}.`, title);
      } }, field(ctx, "category", "kind (feel, idea…)"), field(ctx, "text", `Something to note in ${title}`), buttons);
    }
    return el("form", { class: "head", onsubmit: (e) => {
      e.preventDefault();
      const t = (d.title || "").trim(), why = (d.why || "").trim();
      if (t && why) save(ctx, "Recording", "memory_decide", { title: t, why }, () => `Decided: ${t}.`, t);
    } }, field(ctx, "title", "What was decided"), field(ctx, "why", "Why ([[Pillars]] it serves)"), buttons);
  };

  const reader = (ctx) => {
    const { el } = ctx.kit, kit = ctx.kit, r = state.read, n = r.note;
    if (!n) return kit.section(r.title, el("div", { class: "meta" }, "Reading…"));
    if (n.error) return kit.section(r.title, el("div", { class: "meta" }, n.error));
    const note = n.note || {}, host = ctx.host;
    return el("section", { class: "section mem-note" },
      el("div", { class: "head" }, el("h1", {}, note.title), note.type ? el("span", { class: "badge" }, note.type) : null,
        el("span", { class: "meta" }, note.path), el("span", { class: "spacer" }),
        el("button", { disabled: ctx.busy, onclick: () => { state.form = "note"; ctx.rerender(); } }, "Note"),
        el("button", { disabled: ctx.busy, onclick: () => { state.form = "decide"; ctx.rerender(); } }, "Decide"),
        el("button", { onclick: () => host.openLink(n.obsidian).catch((e) => ctx.say("Couldn't: " + (e && e.message || e))) }, "Obsidian"),
        el("button", { disabled: ctx.busy, onclick: () => ctx.act("Opening", "file_open", { path: note.path }, (t) => t) }, "Code editor"),
        el("button", { onclick: () => host.message([host.text(`About the design note ${note.title}: `)], { send: false }) }, "Ask about this")),
      state.form ? forms(ctx, note.title) : null,
      body(ctx, n.text || ""),
      (n.backlinks || []).length ? el("div", { class: "meta mem-back" }, "Linked from ", n.backlinks.flatMap((b, i) =>
        [i ? ", " : "", el("button", { class: "mem-link", onclick: () => open(ctx, b) }, b)])) : null);
  };

  const styled = (el) => {
    if (styled.done) return;
    styled.done = true;
    document.head.append(el("style", {}, `
      .mem-grow { flex: 1; min-width: 120px; }
      .mem-note { display: grid; gap: 6px; padding: 12px 14px; align-content: start; }
      .mem-body { display: grid; gap: 6px; max-width: 72ch; line-height: 1.5; }
      .mem-body p { margin: 0; } .mem-h { margin: 8px 0 0; font-size: 13px; color: var(--muted); }
      .mem-list { margin: 0; padding-left: 18px; display: grid; gap: 3px; }
      .mem-link { all: unset; color: var(--accent); cursor: pointer; border-bottom: 1px solid transparent; }
      .mem-link:hover, .mem-link:focus-visible { border-bottom-color: currentColor; }
      .mem-file { font: 12px ui-monospace, Menlo, monospace; }
      .mem-back { margin-top: 6px; }`));
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    styled(el);
    if (!ctx.host.can("callTool")) return kit.empty("This host can't run Journeyman's tools here.");
    if (state.game !== game.path) state = fresh(game.path);
    if (state.asking !== game.path + "#" + ctx.visit) { state.asking = game.path + "#" + ctx.visit; list(ctx, game.path); }
    const route = ctx.params.note ? `${ctx.visit}:${ctx.params.note}` : "";
    if (route && state.routed !== route) { state.routed = route; open(ctx, ctx.params.note); }  // /memory?note=Pillars
    const listed = state.listed && state.listed.path === game.path ? state.listed : null;
    if (!listed) return kit.empty("Reading the design notes…");
    if (!listed.notes.length) {
      return kit.empty(listed.error || "No design notes yet.",
        el("button", { class: "primary", onclick: () => ctx.host.message("Interview me about the game I want to make and write its design notes (new_game_interview).", { send: false }) }, "Plan it with the agent"),
        el("button", { disabled: ctx.busy, onclick: () => { state.read = null; save(ctx, "Making the notes", "memory_note", { note: "Notes", text: "Started the design notes." }, () => "The design notes are made.", "Design"); } }, "Start the notes"));
    }
    if (state.read && state.read.note && !state.read.note.error) {
      ctx.share(`The person is reading the design note ${state.read.title} of ${game.name}.`, { note: state.read.title, path: state.read.note.note && state.read.note.note.path });
    }
    // The scaffold's notes in its order (Design's), then the person's own; decisions apart.
    const rank = (n) => { const i = ["Design", "Pitch", "Pillars", "Levels", "Systems", "Decisions", "Notes"].indexOf(n.title); return i < 0 ? 99 : i; };
    const groups = { Design: [], Decisions: [] };
    for (const n of listed.notes.slice().sort((a, b) => rank(a) - rank(b) || a.title.localeCompare(b.title))) groups[/\/decisions\//.test(n.path) ? "Decisions" : "Design"].push(n);
    if (!groups.Decisions.length) delete groups.Decisions;
    const items = Object.entries(groups).map(([label, notes]) => ({ id: "group:" + label, label, meta: String(notes.length),
      children: notes.map((n) => ({ id: n.title, label: n.title, title: n.path, meta: n.observations.length ? `${n.observations.length} noted` : "" })) }));
    const box = el("input", { type: "search", placeholder: "Search the notes", "aria-label": "Search the notes", "data-key": "mem-search", class: "mem-grow",
      onkeydown: (e) => { if (e.key === "Enter") { e.preventDefault(); search(ctx, e.target.value); } else if (e.key === "Escape") search(ctx, ""); } });
    box.value = state.query;
    const left = el("div", { class: "pane" }, el("div", { class: "head" }, box),
      state.found ? kit.section(`Search: ${state.found.text}`, state.found.hits.length ? kit.tree({ label: "Found", items: state.found.hits.map((h, i) => ({
        id: "hit:" + i, label: h.text || h.title, title: h.path, meta: h.line ? h.title : "" })), onPick: (item) => open(ctx, state.found.hits[Number(item.id.slice(4))].title) })
        : el("div", { class: "meta" }, "Nothing found.")) : null,
      kit.section("Design notes", kit.tree({ label: "Design notes", items, selected: state.read && state.read.title, onPick: (item) => { if (!item.children) open(ctx, item.id); } })));
    return el("div", {}, kit.split("memory", left, state.read ? reader(ctx) : kit.section("Note", el("div", { class: "meta" }, "Pick a note.")), { ratio: 0.3 }));
  };

  return { id: "memory", title: "Memory", order: 60, render };
})());
