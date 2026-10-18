// Memory: the game's design notes (design/, jm memory): read one with its
// [[links]] and game files followed, what it connects to in the world, note
// something in it, record a decision, start a playbook on it; the facts kept in mind.
// Writing at length is for Obsidian or the code editor, which this opens.
studioTabs.push((() => {
  "use strict";
  // listed: the memory answer ({ path, notes } or { path, error }), asked each visit.
  // read: { title, note: memory's answer for it | null, world: world_context's | null }. found: a search's { text, hits }.
  // facts: recall's facts (the person's and the game's), asked each visit.
  // form: "note" | "decide" while that form is open; draft: its fields.
  // game: whose these are (another game's start over). seq: the latest list asked (an older answer is dropped).
  const fresh = (game) => ({ game, listed: null, asking: "", read: null, found: null, query: "", form: null, draft: {}, routed: "", seq: 0, searchSeq: 0, facts: null, recallSeq: 0 });
  let state = fresh(null);

  // A call's arguments naming the game shown: another caller opening a game meanwhile can't redirect it.
  const on = (ctx, args = {}) => ({ ...args, game: ctx.game.path });
  const said = (r) => ((r && r.content) || []).filter((c) => c.type === "text").map((c) => c.text).join(" ");

  const list = async (ctx, path) => {
    const n = ++state.seq, mine = state;
    try {
      const r = await ctx.call("memory", on(ctx, {}));
      if (n !== mine.seq || mine !== state) return;
      const s = r && r.structuredContent;
      state.listed = { path, notes: s && Array.isArray(s.notes) ? s.notes : [], error: r && r.isError ? said(r) : "" };
    } catch (e) { if (n !== mine.seq || mine !== state) return; state.listed = { path, notes: [], error: String(e && e.message || e) }; }
    recall(ctx, mine);
    if (!state.read && state.listed.notes.length) open(ctx, (state.listed.notes.find((n) => n.title === "Design") || state.listed.notes[0]).title);
    ctx.rerender();
  };

  const recall = async (ctx, mine) => {
    const n = ++mine.recallSeq;
    const r = await ctx.call("recall", on(ctx, {})).catch(() => null);
    if (mine !== state || n !== mine.recallSeq) return;  // another game's, or an older recall
    state.facts = (r && r.structuredContent && r.structuredContent.facts) || [];
    ctx.rerender();
  };

  const open = async (ctx, title) => {
    const read = state.read = { title, note: null, world: null };
    state.form = null;
    ctx.rerender();
    const [r, w] = await Promise.all([ctx.call("memory", on(ctx, { note: title })).catch(() => null),
      ctx.call("world_context", on(ctx, { note: title, steps: 1 })).catch(() => null)]);
    if (state.read === read) {
      read.note = r && !r.isError && r.structuredContent ? r.structuredContent : { error: said(r) || `No note ${title}.` };
      read.world = w && !w.isError && w.structuredContent ? w.structuredContent : null;
      ctx.rerender();
    }
  };

  // A playbook on this note, for the agent in the chat (the person sends it).
  const playbook = (ctx, name, args, label) => ctx.kit.el("button", { onclick: () => ctx.host.message(
    [ctx.host.text(`${label}: follow the playbook ${name} (playbook {"name": "${name}", "args": ${JSON.stringify(args)}}).`)], { send: false }) }, label);

  // How the note connects: each tie once, either end a link; names linked with no note yet, to write.
  const world = (ctx, path, w) => {
    const { el } = ctx.kit, unwritten = (w && w.unwritten) || [], seen = new Set();
    // A tie stated from both ends alike (road_to both ways, located_in and its contains) is one: its key, read from either end.
    const tie = (e) => {
      const attrs = JSON.stringify(Object.entries(e.attrs || {}).sort()), here = [e.from, e.type, e.to], there = e.inverse ? [e.to, e.inverse, e.from] : here;
      return JSON.stringify([here, there].sort((a, b) => JSON.stringify(a).localeCompare(JSON.stringify(b)))[0]) + attrs;
    };
    const edges = ((w && w.edges) || []).filter((e) => !seen.has(tie(e)) && seen.add(tie(e)));
    if (!edges.length && !unwritten.length) return null;
    // An end opens by its note's path (two notes may share a title: North/Harbor); an unwritten one by its name.
    const end = (name, at) => at && at === path ? el("b", {}, name) : el("button", { class: "mem-link", onclick: () => open(ctx, at || name) }, name);
    const details = (e) => Object.entries(e.attrs || {}).map(([k, v]) => `${k}: ${v}`).join(", ");
    return el("div", { class: "mem-world" }, el("h3", { class: "mem-h" }, "World"),
      el("ul", { class: "mem-list" }, edges.map((e) => el("li", {}, end(e.from, e.fromPath), ` ${e.type.replace(/_/g, " ")} `, end(e.to, e.toPath),
        details(e) ? el("span", { class: "meta" }, ` (${details(e)})`) : null))),
      unwritten.length ? el("div", { class: "meta" }, "Linked, no note yet: ", unwritten.flatMap((u, i) => [i ? ", " : "",
        el("button", { class: "mem-link", onclick: () => ctx.host.message([ctx.host.text(`Write a dossier for ${u}, from what links to it (dossier_new).`)], { send: false }) }, u)])) : null);
  };

  // A dossier's properties, [[links]] followed.
  const props = (ctx, p) => {
    const { el } = ctx.kit, rows = Object.entries(p || {}).filter(([, v]) => v !== "" && !(Array.isArray(v) && !v.length));
    if (!rows.length) return null;
    return el("dl", { class: "mem-props" }, rows.flatMap(([k, v]) => [el("dt", {}, k), el("dd", {}, inline(ctx, Array.isArray(v) ? v.join(", ") : String(v)))]));
  };

  // What's kept in mind (recall), each forgettable; the agent remembers more.
  const facts = (ctx) => {
    const { el, section } = ctx.kit, all = state.facts;
    if (!all) return null;
    const forget = async (f) => {
      const mine = state;
      if (!(await ctx.act("Forgetting", "forget", on(ctx, { id: f.id, scope: f.scope }), (t) => t)) || mine !== state) return;
      recall(ctx, mine);
      if (f.scope === "game" && state.read && state.read.note && state.read.note.note && state.read.note.note.path === "design/Memory.md") open(ctx, state.read.title);  // the page it was on
    };
    return section("Keep in mind", all.length ? el("ul", { class: "mem-list mem-facts" }, all.map((f) => el("li", {},
      el("span", { class: "badge" }, f.category), el("span", {}, inline(ctx, f.text), f.scope === "you" ? el("span", { class: "meta" }, " (yours)") : null),
      el("button", { class: "mem-forget", title: "Forget this", "aria-label": `Forget: ${f.text}`, disabled: ctx.busy, onclick: () => forget(f) }, "×"))))
      : el("div", { class: "meta" }, "Nothing yet: ask the agent to remember canon, rules and how you like to work."));
  };

  const search = async (ctx, text) => {
    state.query = text;
    if (!text.trim()) { state.searchSeq++; state.found = null; ctx.rerender(); return; }
    const n = ++state.searchSeq, mine = state;
    const r = await ctx.call("memory_search", on(ctx, { text })).catch(() => null);
    if (mine !== state || n !== state.searchSeq) return;  // another game's, or an older search
    state.found = { text, hits: (r && r.structuredContent && r.structuredContent.hits) || [] };
    ctx.rerender();
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

  // Saved: reopen, else the note as the tool named it (a title can lose a ? or :). Not: the form stays, to try again.
  const save = async (ctx, label, name, args, done, reopen) => {
    let saved = null;
    const mine = state;  // another game shown meanwhile: its tab isn't this one's to touch
    const ok = await ctx.act(label, name, on(ctx, args), (text) => {
      try { saved = JSON.parse(text).note || null; } catch (e) {}
      return done(saved);
    });
    if (!ok || mine !== state) return;
    state.form = null;
    state.draft = {};
    await list(ctx, ctx.game.path);
    if (mine !== state) return;
    open(ctx, reopen || saved || args.title || args.note);  // a host that gave no text: as typed
  };

  const forms = (ctx, title) => {
    const { el } = ctx.kit, d = state.draft;
    const buttons = [el("button", { class: "primary", type: "submit", disabled: ctx.busy }, state.form === "note" ? "Note it" : "Record"),
      el("button", { type: "button", onclick: () => { state.form = null; ctx.rerender(); } }, "Cancel")];
    if (state.form === "note") {
      return el("form", { class: "head", onsubmit: (e) => {
        e.preventDefault();
        if ((d.text || "").trim()) save(ctx, "Noting", "memory_note", { note: title, text: d.text.trim(), category: (d.category || "").trim() }, (note) => `Noted in ${note || title}.`);
      } }, field(ctx, "category", "kind (feel, idea…)"), field(ctx, "text", `Something to note in ${title}`), buttons);
    }
    return el("form", { class: "head", onsubmit: (e) => {
      e.preventDefault();
      const t = (d.title || "").trim(), why = (d.why || "").trim();
      if (t && why) save(ctx, "Recording", "memory_decide", { title: t, why }, (note) => `Decided: ${note || t}.`);
    } }, field(ctx, "title", "What was decided"), field(ctx, "why", "Why ([[Pillars]] it serves)"), buttons);
  };

  const reader = (ctx) => {
    const { el } = ctx.kit, kit = ctx.kit, r = state.read, n = r.note;
    if (!n) return kit.section(r.title, el("div", { class: "meta" }, "Reading…"));
    if (n.error) return kit.section(r.title, el("div", { class: "meta" }, n.error));
    const note = n.note || {}, host = ctx.host;
    // The note as a playbook names it: its title, or its path when another note has that title.
    const shared = ((state.listed && state.listed.notes) || []).filter((o) => o.title.toLowerCase() === note.title.toLowerCase()).length > 1, which = shared ? note.path : note.title;
    return el("section", { class: "section mem-note" },
      el("div", { class: "head" }, el("h1", {}, note.title), note.type ? el("span", { class: "badge" }, note.type) : null,
        el("span", { class: "meta" }, note.path)),
      // What to do with it: ask first, add to it, then open it elsewhere.
      el("div", { class: "head mem-acts" },
        el("button", { class: "primary", onclick: () => host.message([host.text(`About the design note ${note.title}: `)], { send: false }) }, "Ask about this"),
        el("button", { disabled: ctx.busy, onclick: () => { state.form = "note"; ctx.rerender(); } }, "Note"),
        el("button", { disabled: ctx.busy, onclick: () => { state.form = "decide"; ctx.rerender(); } }, "Decide"),
        el("span", { class: "spacer" }),
        el("button", { onclick: () => host.openLink(n.obsidian).catch((e) => ctx.say("Couldn't: " + (e && e.message || e))) }, "Obsidian"),
        el("button", { disabled: ctx.busy, onclick: () => ctx.act("Opening", "file_open", on(ctx, { path: note.path }), (t) => t) }, "Code editor")),
      el("div", { class: "head mem-plays" }, el("span", { class: "meta" }, "Playbooks:"),
        playbook(ctx, "expand_world", { note: which }, "Grow the world"),
        note.type === "location" ? playbook(ctx, "level_from_place", { place: which }, "Make it a level") : null,
        ["character", "item", "location", "faction"].includes(note.type) ? playbook(ctx, "art_pass", { note: which }, "Draw it") : null,
        playbook(ctx, "canon_check", { note: which }, "Check canon")),
      state.form ? forms(ctx, note.title) : null,
      props(ctx, note.props),
      body(ctx, n.text || ""),
      world(ctx, note.path, r.world),
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
      .mem-back { margin-top: 6px; }
      .mem-plays { flex-wrap: wrap; gap: 6px; } .mem-acts { margin-top: -6px; }
      .mem-props { display: grid; grid-template-columns: max-content 1fr; gap: 2px 12px; margin: 0; font-size: 13px; }
      .mem-props dt { color: var(--muted); } .mem-props dd { margin: 0; }
      .mem-world { display: grid; gap: 4px; max-width: 72ch; }
      .mem-facts { list-style: none; padding: 0; } .mem-facts li { display: flex; gap: 6px; align-items: baseline; }
      .mem-forget { all: unset; margin-left: auto; cursor: pointer; color: var(--muted); padding: 0 4px; } .mem-forget:hover { color: var(--accent); }`));
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    styled(el);
    if (!ctx.host.can("callTool")) return kit.empty("This host can't run Journeyman's tools here.");
    if (state.game !== game.path) state = fresh(game.path);
    if (state.asking !== game.path + "#" + ctx.visit) { state.asking = game.path + "#" + ctx.visit; list(ctx, game.path); }
    const route = ctx.params.note ? `${ctx.nav}:${ctx.params.note}` : "";
    if (route && state.routed !== route) { state.routed = route; open(ctx, ctx.params.note); }  // /memory?note=Pillars
    const listed = state.listed && state.listed.path === game.path ? state.listed : null;
    if (!listed) return kit.empty("Reading the design notes…");
    if (!listed.notes.length) {
      return kit.empty(listed.error || "No design notes yet.",
        el("button", { class: "primary", onclick: () => ctx.host.message("Plan the game I want to make with me, a question at a time (onboard).", { send: false }) }, "Plan it with the agent"),
        el("button", { disabled: ctx.busy, onclick: () => { state.read = null; save(ctx, "Making the notes", "memory_note", { note: "Notes", text: "Started the design notes." }, () => "The design notes are made.", "Design"); } }, "Start the notes"), facts(ctx));
    }
    if (state.read && state.read.note && !state.read.note.error) {
      ctx.share(`The person is reading the design note ${state.read.title} of ${game.name}.`, { note: state.read.title, path: state.read.note.note && state.read.note.note.path });
    }
    // The scaffold's notes in its order (Design's), then the person's own; dossiers by their folder; decisions last.
    const rank = (n) => { const i = ["Design", "Pitch", "Pillars", "Levels", "Systems", "Decisions", "Notes"].indexOf(n.title); return i < 0 ? 99 : i; };
    const groups = new Map([["Design", []]]);  // a folder may be named anything (constructor, too)
    for (const n of listed.notes.slice().sort((a, b) => rank(a) - rank(b) || a.title.localeCompare(b.title))) {
      const folder = (/^design\/([^/]+)\//.exec(n.path) || [])[1], label = !folder ? "Design" : folder === "decisions" ? "Decisions" : folder;
      if (!groups.has(label)) groups.set(label, []);
      groups.get(label).push(n);
    }
    const decisions = groups.get("Decisions");
    if (decisions) { groups.delete("Decisions"); groups.set("Decisions", decisions); }
    const items = [...groups].map(([label, notes]) => ({ id: "group:" + label, label, meta: String(notes.length),
      children: notes.map((n) => ({ id: n.title, label: n.title, title: n.path, meta: n.observations.length ? `${n.observations.length} noted` : "" })) }));
    const box = el("input", { type: "search", placeholder: "Search the notes", "aria-label": "Search the notes", "data-key": "mem-search", class: "mem-grow",
      onkeydown: (e) => { if (e.key === "Enter") { e.preventDefault(); search(ctx, e.target.value); } else if (e.key === "Escape") search(ctx, ""); } });
    box.value = state.query;
    const left = el("div", { class: "pane" }, el("div", { class: "head" }, box),
      state.found ? kit.section(`Search: ${state.found.text}`, state.found.hits.length ? kit.tree({ label: "Found", items: state.found.hits.map((h, i) => ({
        id: "hit:" + i, label: h.text || h.title, title: h.path, meta: h.line ? h.title : "" })), onPick: (item) => open(ctx, state.found.hits[Number(item.id.slice(4))].title) })
        : el("div", { class: "meta" }, "Nothing found.")) : null,
      kit.section("Design notes", kit.tree({ label: "Design notes", items, selected: state.read && state.read.title, onPick: (item) => { if (!item.children) open(ctx, item.id); } })),
      facts(ctx));
    return el("div", {}, kit.split("memory", left, state.read ? reader(ctx) : kit.section("Note", el("div", { class: "meta" }, "Pick a note.")), { ratio: 0.3 }));
  };

  return { id: "memory", title: "Memory", order: 60, render };
})());
