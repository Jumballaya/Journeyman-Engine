// Files: the open game's files as a tree with git's marks, a search, and a
// quick look at one (what's wrong with it, its text or picture); editing is
// for the editor and the person's code editor, which this opens.
studioTabs.push((() => {
  "use strict";
  // listed: the files answer ({ path, root, files } or { path, error }), asked each visit.
  // picked: { path, file: file_read's answer | null }. found: { text, hits } of a search.
  // draft: { kind: "new" | "rename", value } while that form is open. folded: per game.
  // game: whose these are (another game's start over). seq: the latest request (an older answer is dropped).
  const fresh = (game) => ({ game, listed: null, asking: "", picked: null, found: null, query: "", draft: null, folded: null, line: 0, routed: "", seq: 0, searchSeq: 0 });
  let state = fresh(null);
  const marks = { M: "changed", A: "added", "?": "new", D: "deleted", U: "conflict" };

  // A call's arguments naming the game shown: another caller opening a game meanwhile can't redirect it.
  const said = (r) => ((r && r.content) || []).filter((c) => c.type === "text").map((c) => c.text).join(" ");

  const list = async (ctx, path) => {
    const n = ++state.seq, mine = state;
    try {
      const r = await ctx.call("files", {});
      if (n !== mine.seq || mine !== state) return;
      const s = r && r.structuredContent;
      state.listed = s && Array.isArray(s.files) ? { path, game: s.game, files: s.files } : { path, error: said(r) || "No files." };
    } catch (e) { if (n !== mine.seq || mine !== state) return; state.listed = { path, error: String(e && e.message || e) }; }
    if (state.picked && !state.listed.error && !state.listed.files.some((f) => f.path === state.picked.path)) state.picked = null;
    ctx.rerender();
  };

  const pick = async (ctx, path, line = 0) => {
    const picked = state.picked = { path, file: null };
    state.line = line;
    state.draft = null;
    ctx.rerender();
    const r = await ctx.call("file_read", { path }).catch((e) => ({ isError: true, content: [{ type: "text", text: String(e && e.message || e) }] }));
    if (state.picked === picked) {
      picked.file = r && !r.isError ? { ...(r.structuredContent || {}), image: r._meta && r._meta["jm/image"] } : { error: said(r) || "Couldn't read it." };
      ctx.rerender();
    }
  };

  const search = async (ctx, text) => {
    state.query = text;
    if (!text.trim()) { state.searchSeq++; state.found = null; ctx.rerender(); return; }
    const n = ++state.searchSeq, mine = state;
    const r = await ctx.call("files_search", { text }).catch(() => null);
    if (mine !== state || n !== state.searchSeq) return;  // another game's, or an older search
    state.found = { text, ...(r && r.structuredContent ? r.structuredContent : { hits: [] }) };
    ctx.rerender();
  };

  // Paths as a tree by folder: a folder's meta counts the changes in it.
  const tree = (files) => {
    const root = { children: [] }, folders = [];
    for (const f of files) {
      let at = root;
      const parts = f.path.split("/");
      parts.forEach((part, i) => {
        const leaf = i === parts.length - 1;
        const id = parts.slice(0, i + 1).join("/") + (leaf ? "" : "/");  // a folder's ends in "/": git may list a file and a folder of one name
        let next = at.children.find((c) => c.id === id);
        if (!next) {
          at.children.push(next = leaf ? { id, label: part, file: f, title: f.path } : { id, label: part, children: [], changed: 0 });
          if (!leaf) folders.push(next);
        }
        if (!leaf && f.status) next.changed++;
        at = next;
      });
    }
    const order = (list) => {
      list.sort((a, b) => (!a.children - !b.children) || a.label.localeCompare(b.label));
      for (const n of list) {
        if (n.children) { order(n.children); n.meta = n.changed ? `${n.changed} changed` : ""; }
        else n.meta = [n.file.status ? marks[n.file.status] || n.file.status : "", n.file.status === "D" ? "" : size(n.file.size)].filter(Boolean).join(" · ");
      }
    };
    order(root.children);
    return { items: root.children, folders };
  };

  const size = (n) => n >= 1 << 20 ? `${(n / (1 << 20)).toFixed(1)} MB` : n >= 1024 ? `${(n / 1024).toFixed(1)} KB` : `${n} B`;

  // Runs a file tool; then the list and the quick look are read again.
  const change = async (ctx, label, name, args, done, then) => {
    const mine = state;  // another game shown meanwhile: its tab isn't this one's to touch
    const ok = await ctx.act(label, name, args, done);
    if (mine !== state) return;
    if (ok) { state.draft = null; if (then) then(); }
    await list(ctx, ctx.game.path);
  };

  // A file from the person's computer into assets/: given to the host (its
  // upload), which asset_add then fetches; a host without uploads gets its
  // bytes (asset_add's data, up to 8 MB).
  const addFile = (ctx) => {
    const { el } = ctx.kit;
    const input = el("input", { type: "file", hidden: true, "data-key": "files-add", accept: ".png,.jpg,.jpeg,.aseprite,.ase,.tmj,.tsj,.json,.wav,.ogg,.mp3,.ttf,.otf",
      onchange: (e) => { const file = e.target.files && e.target.files[0]; e.target.value = ""; if (file) upload(ctx, file); } });
    return [input, el("button", { disabled: ctx.busy, onclick: () => input.click() }, "Add file")];
  };
  const inlineLimit = 8 << 20;
  const base64 = (buffer) => {
    const bytes = new Uint8Array(buffer);
    let text = "";
    for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(text);
  };
  const upload = async (ctx, file) => {
    const mine = state;
    let args;
    if (ctx.host.can("uploadFile")) {
      let given;
      try { given = await ctx.host.uploadFile(file); } catch (e) { ctx.say("Couldn't upload it: " + (e && e.message || e)); return; }
      args = { file: { download_url: given.downloadUrl, file_id: given.fileId, file_name: file.name, mime_type: file.type || undefined } };
    } else {
      if (file.size > inlineLimit) { ctx.say(`${file.name} is over 8 MB: attach it to the chat instead.`); return; }
      try { args = { data: base64(await file.arrayBuffer()), path: "assets/" + file.name }; } catch (e) { ctx.say("Couldn't read it: " + (e && e.message || e)); return; }
    }
    // Still to the game it was picked for; another game shown since keeps its own tab.
    if (mine !== state) await ctx.act("Adding the file", "asset_add", args, (t) => t);
    else await change(ctx, "Adding the file", "asset_add", args, (t) => t, () => pick(ctx, "assets/" + file.name));
  };

  const form = (ctx, placeholder, submit) => {
    const { el } = ctx.kit;
    const input = el("input", { placeholder, "aria-label": placeholder, "data-key": "files-draft", class: "files-grow",
      oninput: (e) => { state.draft.value = e.target.value; },
      onkeydown: (e) => { if (e.key === "Escape") { state.draft = null; ctx.rerender(); } } });
    input.value = state.draft.value;
    return el("form", { class: "head", onsubmit: (e) => { e.preventDefault(); const v = state.draft.value.trim(); if (v) submit(v); } },
      input, el("button", { class: "primary", type: "submit", disabled: ctx.busy }, "OK"),
      el("button", { type: "button", onclick: () => { state.draft = null; ctx.rerender(); } }, "Cancel"));
  };

  const searchBox = (ctx) => {
    const { el } = ctx.kit;
    const input = el("input", { type: "search", placeholder: "Search names and lines", "aria-label": "Search the game", "data-key": "files-search",
      class: "files-grow", onkeydown: (e) => { if (e.key === "Enter") { e.preventDefault(); search(ctx, e.target.value); } else if (e.key === "Escape") search(ctx, ""); } });
    input.value = state.query;
    return input;
  };

  const results = (ctx) => {
    const { el } = ctx.kit, f = state.found;
    if (!f.hits.length) return el("div", { class: "meta" }, `Nothing holds “${f.text}”.`);
    return el("div", { class: "files-hits" }, f.hits.map((h, i) => el("button", { class: "files-hit", "data-key": "hit:" + i, onclick: () => pick(ctx, h.path, h.line || 0) },
      el("span", { class: "files-path" }, h.line ? `${h.path}:${h.line}` : h.path), h.text ? el("span", { class: "meta" }, h.text) : null)),
      f.truncated ? el("div", { class: "meta" }, "The first 200: search for something narrower.") : null);
  };

  // The quick look: what's wrong, then the text (lines numbered) or the picture.
  const look = (ctx) => {
    const { el } = ctx.kit, p = state.picked, f = p.file;
    if (!f) return el("div", { class: "meta" }, "Reading…");
    if (f.error) return el("div", { class: "meta" }, f.error);
    const body = [];
    if ((f.problems || []).length) body.push(el("ul", { class: "files-problems" }, f.problems.map((x) => el("li", {}, x))));
    else if (/\.json$/.test(p.path)) body.push(el("div", { class: "files-ok" }, "Valid, in jm fmt's layout."));
    if (f.image) body.push(el("img", { class: "files-image", src: f.image, alt: p.path, onload: (e) => {  // pixel art: shown big, unblurred
      const img = e.target, k = Math.max(1, Math.min(8, Math.floor(320 / Math.max(img.naturalWidth, img.naturalHeight, 1))));
      img.style.width = img.naturalWidth * k + "px";
      img.title = `${img.naturalWidth} × ${img.naturalHeight}${k > 1 ? `, shown ×${k}` : ""}`;
    } }));
    else if (f.binary) body.push(el("div", { class: "meta" }, `Not text (${size(f.size)}).`));
    else {
      const lines = f.text.replace(/\n$/, "").split("\n"), shown = 400, from = Math.max(0, Math.min(state.line - 20, lines.length - shown));
      body.push(el("pre", { class: "files-code", "aria-label": p.path }, lines.slice(from, from + shown).map((l, i) =>
        el("span", { class: "files-line" + (from + i + 1 === state.line ? " on" : "") }, el("span", { class: "files-n" }, String(from + i + 1)), l + "\n"))));
      if (lines.length > shown) body.push(el("div", { class: "meta" }, `Lines ${from + 1}–${Math.min(from + shown, lines.length)} of ${lines.length}: the code editor has the rest.`));
    }
    return el("div", { class: "files-look" }, body);
  };

  const actions = (ctx, root) => {
    const { el } = ctx.kit, p = state.picked, busy = ctx.busy, host = ctx.host;
    const scene = /\.scene\.json$/.test(p.path);
    const name = p.path.split("/").pop();
    return [
      scene && ctx.has("scene") ? el("button", { class: "primary", onclick: () => ctx.go(ctx.link("scene", { scene: p.path })) }, "Scene") : null,
      el("button", { disabled: busy, onclick: () => ctx.act("Opening the editor", "project_open_editor", {}, () => `${ctx.game.name} is open in the editor.`) }, "Open in editor"),
      el("button", { disabled: busy, onclick: () => ctx.act("Opening", "file_open", { path: p.path }, (t) => t) }, "Code editor"),
      root && host.can("openai/files") ? el("button", { onclick: () => host.openFile(root + "/" + p.path).catch((e) => ctx.say("Couldn't: " + (e && e.message || e))) }, "File viewer") : null,
      el("button", { disabled: busy, onclick: () => { state.draft = { kind: "rename", value: p.path }; ctx.rerender(); } }, "Rename"),
      ctx.confirm({ key: "delete:" + p.path, label: "Delete", ask: `Delete ${name}?`, tool: "file_delete", args: { path: p.path },
        armed: !!(state.draft && state.draft.kind === "delete"), arm: () => { state.draft = { kind: "delete", value: "" }; ctx.rerender(); },
        go: () => change(ctx, "Deleting", "file_delete", { path: p.path }, () => `${name} is deleted.`, () => { state.picked = null; }) }),
      el("button", { onclick: () => host.message([host.text(`About ${p.path} in ${ctx.game.name}: `)], { send: false }) }, "Ask about this"),
    ];
  };

  const styled = (el) => {
    if (styled.done) return;
    styled.done = true;
    document.head.append(el("style", {}, `
      .files-grow { flex: 1; min-width: 120px; }
      .files-hits { display: grid; max-height: 40vh; overflow: auto; }
      .files-hit { all: unset; display: grid; padding: 3px 6px; border-radius: 6px; cursor: pointer; }
      .files-hit:hover, .files-hit:focus-visible { background: var(--raised); }
      .files-path { font-family: ui-monospace, Menlo, monospace; font-size: 12px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
      .files-look { display: grid; gap: 8px; }
      .files-tree { max-height: 70vh; overflow: auto; }
      @media (max-width: 700px) { .files-view .split > .pane:last-child { order: -1; } .files-tree, .files-code { max-height: 50vh; } }
      .files-side { display: grid; gap: 8px; align-content: start; }
      .files-title { display: flex; gap: 8px; align-items: baseline; min-width: 0; padding: 2px 4px 0; }
      .files-title .files-path { font-size: 13px; font-weight: 600; }
      .files-actions { display: flex; gap: 6px; flex-wrap: wrap; }
      .files-code { margin: 0; font: 12px/1.45 ui-monospace, Menlo, monospace; background: var(--bg); border: 1px solid var(--line);
        border-radius: var(--radius); padding: 6px 0; overflow: auto; max-height: 62vh; tab-size: 2; }
      .files-line { display: block; padding-right: 10px; white-space: pre; } .files-line.on { background: var(--accent-soft); }
      .files-n { display: inline-block; width: 4ch; margin-right: 10px; text-align: right; color: var(--muted); user-select: none; }
      .files-problems { margin: 0; padding-left: 18px; color: var(--warn); } .files-ok { color: var(--muted); font-size: 12px; }
      .files-image { max-width: 100%; image-rendering: pixelated; border-radius: var(--radius); justify-self: start; padding: 8px;
        background: repeating-conic-gradient(var(--raised) 0 25%, var(--bg) 0 50%) 0 0 / 16px 16px; }`));
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    styled(el);
    if (!ctx.host.can("callTool")) return kit.empty("This host can't run Journeyman's tools here.");
    if (state.game !== game.path) state = fresh(game.path);
    if (state.asking !== game.path + "#" + ctx.visit) { state.asking = game.path + "#" + ctx.visit; list(ctx, game.path); }
    const route = ctx.params.file ? `${ctx.nav}:${ctx.params.file}:${ctx.params.line}` : "";
    if (route && state.routed !== route) {  // a deep link: /files?file=scenes/main.scene.json&line=3
      state.routed = route;
      pick(ctx, ctx.params.file, Number(ctx.params.line) || 0);
    }
    const listed = state.listed && state.listed.path === game.path ? state.listed : null;
    if (!listed) return kit.empty("Reading the files…");
    if (listed.error) return kit.empty(listed.error);
    const { items, folders } = tree(listed.files);
    if (!state.folded || state.folded.game !== game.path) {  // folders open where something changed
      state.folded = { game: game.path, ids: new Set(folders.filter((f) => !f.changed).map((f) => f.id)) };
    }
    const changed = listed.files.filter((f) => f.status).length;
    const p = state.picked;
    if (p) for (let at = p.path.lastIndexOf("/"); at > 0; at = p.path.lastIndexOf("/", at - 1)) state.folded.ids.delete(p.path.slice(0, at + 1));  // its folders open
    if (p) ctx.share(`The person is looking at ${p.path} in ${game.name}'s files.`, { file: p.path, problems: p.file && p.file.problems });

    const head = el("div", { class: "head" }, el("h1", {}, "Files"),
      el("span", { class: "meta" }, `${listed.files.length} files${changed ? ` · ${changed} changed since the last commit` : ""}`),
      el("span", { class: "spacer" }),
      ctx.host.can("uploadFile") || ctx.host.can("callTool") ? addFile(ctx) : null,
      el("button", { disabled: ctx.busy, onclick: () => { state.draft = { kind: "new", value: "" }; ctx.rerender(); } }, "New file"));
    const draft = state.draft && state.draft.kind === "new"
      ? form(ctx, "Path of the new file, e.g. assets/data/items.json", (path) =>
          change(ctx, "Making the file", "file_write", { path, text: /\.json$/.test(path) ? "{}" : "", etag: "" }, () => `${path} is made.`, () => pick(ctx, path)))
      : state.draft && state.draft.kind === "rename" && p
        ? form(ctx, "Its new path", (to) => change(ctx, "Renaming", "file_move", { from: p.path, to }, () => `Renamed to ${to}: scenes naming the old path need changing (Search finds them).`, () => pick(ctx, to)))
        : null;
    const left = el("div", { class: "pane" }, el("div", { class: "head" }, searchBox(ctx)),
      state.found ? kit.section(`Search: ${state.found.text}`, results(ctx)) : null,
      kit.section("Files", el("div", { class: "files-tree" }, kit.tree({ label: "Files", items, folded: state.folded.ids, selected: p && p.path,
        onPick: (item) => item.file ? pick(ctx, item.id) : false }))));
    const entry = p && listed.files.find((f) => f.path === p.path);
    const right = p
      ? el("section", { class: "section files-side" },
          el("div", { class: "files-title" }, el("span", { class: "files-path", title: p.path }, p.path),
            entry && entry.status ? el("span", { class: "badge" }, marks[entry.status] || entry.status) : null,
            entry && entry.status !== "D" ? el("span", { class: "meta" }, size(entry.size)) : null),
          el("div", { class: "files-actions" }, actions(ctx, listed.game)), look(ctx))
      : kit.section("Quick look", el("div", { class: "meta" }, "Pick a file to see it, and what's wrong with it."));
    return el("div", { class: "files-view" }, head, draft, kit.split("files", left, right, { ratio: 0.38 }));
  };

  return { id: "files", title: "Files", order: 40, render };
})());
