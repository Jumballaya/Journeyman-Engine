// File: a prefab, an atlas or key bindings (file_view), the ones ChatGPT
// desktop opens and those picked in Index. Key bindings opened by the host
// are edited here and saved through it (as jm fmt writes them).
studioTabs.push((() => {
  "use strict";
  // viewed: file_view answers by path and visit. saved: the host-opened file as
  // read for writing ({ uri, doc, etag, writable, why }). picked: an entity or image.
  const state = { viewed: {}, saved: null, reading: "", picked: null, capture: null, notice: "" };

  // The engine's control names (engine/inputs) from a browser key's code.
  const keyName = (code) => {
    const m = /^Key([A-Z])$|^(Digit\d)$|^(F\d+)$|^Arrow(Up|Down|Left|Right)$/.exec(code);
    if (m) return m[1] || m[2] || m[3] || "Arrow" + m[4];
    const named = { Backquote: "Backtick", BracketLeft: "LeftBracket", BracketRight: "RightBracket", Quote: "Apostrophe",
      NumpadDecimal: "KPPeriod", NumpadEnter: "KPEnter", NumpadAdd: "KPAdd", NumpadSubtract: "KPSubtract",
      NumpadMultiply: "KPMultiply", NumpadDivide: "KPDivide", ShiftLeft: "LeftShift", ShiftRight: "RightShift",
      ControlLeft: "LeftCtrl", ControlRight: "RightCtrl", AltLeft: "LeftAlt", AltRight: "RightAlt", MetaLeft: "LeftSuper", MetaRight: "RightSuper" };
    if (named[code]) return named[code];
    if (/^Numpad\d$/.test(code)) return "KP" + code.slice(6);
    return /^(Minus|Equal|Backslash|Semicolon|Comma|Period|Slash|Escape|Tab|Enter|Space|Backspace|Insert|Delete|Home|End|PageUp|PageDown|CapsLock|NumLock|ScrollLock|PrintScreen|Pause)$/.test(code) ? code : null;
  };
  const pads = ["A", "B", "X", "Y", "LeftBumper", "RightBumper", "Back", "Start", "Guide", "LeftThumb", "RightThumb", "DPadUp", "DPadRight",
    "DPadDown", "DPadLeft", "LeftStickLeft", "LeftStickRight", "LeftStickUp", "LeftStickDown", "RightStickLeft", "RightStickRight",
    "RightStickUp", "RightStickDown", "LeftTrigger", "RightTrigger"].map((p) => "Gamepad." + p).concat(["MouseLeft", "MouseRight", "MouseMiddle"]);

  // The file shown: the host's answer (file_view called), else asked for by path once a visit.
  const current = (ctx) => {
    const own = ctx.host.result && ctx.host.result.structuredContent && ctx.host.result.structuredContent.file;
    const path = ctx.params.path;
    if (own && (!path || own.path === path)) return { file: own, thumbs: (ctx.host.result._meta && ctx.host.result._meta["jm/files"]) || {} };
    if (!path) return null;
    const key = `${path}|${ctx.visit}`;
    if (!state.viewed[key]) {
      state.viewed[key] = { loading: true };
      ctx.call("file_view", { path }).then((r) => {
        const file = r && r.structuredContent && r.structuredContent.file;
        state.viewed[key] = file ? { file, thumbs: (r._meta && r._meta["jm/files"]) || {} } : { error: (r && r.content && r.content[0] && r.content[0].text) || "Couldn't open it." };
        ctx.rerender();
      }, (e) => { state.viewed[key] = { error: String(e && e.message || e) }; ctx.rerender(); });
    }
    return state.viewed[key];
  };

  // The host-opened file, read once for its version: only a file that reads back as jm fmt writes it is changed here.
  const opened = (ctx) => ctx.host.input && ctx.host.input.file && ctx.host.input.file.resourceUri;
  const readOpened = async (ctx, uri) => {
    state.reading = uri;
    try {
      const r = await ctx.host.readResource(uri);
      const doc = JSON.parse(r.text);
      state.saved = { uri, doc, etag: r.etag, writable: r.writable && !!r.etag && ctx.kit.jsonText(doc) + "\n" === r.text,
                      why: !r.writable || !r.etag ? "the host won't save it" : "it isn't as jm fmt writes it (jm fmt it)" };
    } catch (e) { state.notice = "Couldn't read the file: " + (e && e.message || e); }
    ctx.rerender();
  };

  const save = async (ctx, change) => {
    const s = state.saved, doc = JSON.parse(JSON.stringify(s.doc));
    change(doc.actions);
    state.notice = "Saving…"; ctx.rerender();
    try {
      const r = await ctx.host.writeResource(s.uri, ctx.kit.jsonText(doc) + "\n", s.etag);
      if (r && r.outcome === "saved") { Object.assign(s, { doc, etag: r.etag }); state.notice = "Saved."; }
      else if (r && r.outcome === "conflict") { state.notice = "The file changed since: read again, change it again."; await readOpened(ctx, s.uri); }
      else state.notice = "Couldn't save.";
    } catch (e) { state.notice = "Couldn't save: " + (e && e.message || e); }
    ctx.rerender();
  };

  const bindings = (ctx, file) => {
    const { kit } = ctx, { el } = kit;
    const editable = state.saved && state.saved.writable && state.saved.uri === opened(ctx);
    const actions = editable ? state.saved.doc.actions : file.actions;
    const chip = (action, key) => el("span", { class: "badge", style: { display: "inline-flex", gap: "4px", alignItems: "center" } }, key,
      editable ? el("button", { "aria-label": `Remove ${key} from ${action}`, style: { border: 0, background: "none", padding: 0, color: "inherit" },
        onclick: () => save(ctx, (a) => { a[action] = a[action].filter((k) => k !== key); }) }, "×") : null);
    const add = (action) => (key) => { if (key && !actions[action].includes(key)) save(ctx, (a) => { a[action].push(key); }); };
    const rows = Object.entries(actions).map(([action, keys]) => el("div", { class: "field", style: { gridTemplateColumns: "120px minmax(0, 1fr)" } },
      el("label", { title: action }, action),
      el("div", { class: "vals", style: { flexWrap: "wrap" } }, keys.map((k) => chip(action, k)),
        editable ? el("button", { "data-key": "capture:" + action, title: "Press the key to add",
          onclick: () => { state.capture = action; ctx.rerender(); },
          onkeydown: (e) => {
            if (state.capture !== action) return;
            e.preventDefault();
            state.capture = null;
            add(action)(keyName(e.code));
          } }, state.capture === action ? "Press a key…" : "+ key") : null,
        editable ? el("select", { "aria-label": `Add a gamepad or mouse control to ${action}`, style: { width: "150px", flex: "none" }, onchange: (e) => add(action)(e.target.value) },
          el("option", { value: "" }, "+ pad/mouse"), pads.map((p) => el("option", { value: p }, p))) : null)));
    return kit.section("Actions", el("div", { class: "form" }, rows,
      editable ? el("input", { placeholder: "New action (Enter)", "aria-label": "New action", "data-key": "new-action",
        onkeydown: (e) => { const name = e.target.value.trim(); if (e.key === "Enter" && name && !actions[name]) save(ctx, (a) => { a[name] = []; }); } }) : null));
  };

  const prefab = (ctx, file) => {
    const { kit } = ctx, { el } = kit;
    const picked = file.entities.find((e) => e.where === state.picked) || file.entities[0];
    const depth = (w) => (w.match(/\//g) || []).length;
    return kit.split("file-prefab",
      kit.section("Entities", kit.tree({ label: "Entities", selected: picked && picked.where,
        items: file.entities.map((e) => ({ id: e.where, label: "  ".repeat(depth(e.where)) + (e.name || e.where), meta: e.inherited ? "from the prefab" : "" })),
        onPick: (item) => { state.picked = item.id; ctx.rerender(); } })),
      kit.section(picked ? picked.name : "Components", picked ? Object.entries(picked.components || {}).map(([name, c]) =>
        el("div", {}, el("h2", {}, name), el("pre", { style: { margin: "0 0 8px", fontSize: "11px", whiteSpace: "pre-wrap" } }, kit.jsonText(c)))) : null),
      { ratio: 0.4 });
  };

  const atlas = (ctx, file, thumbs) => {
    const { kit } = ctx;
    return ctx.kit.el("div", { class: "dense" }, kit.grid({ label: "Images", selected: state.picked,
      items: file.images.map((i) => ({ id: i.path, title: i.region, meta: i.size || "", thumb: thumbs[i.path] })),
      onPick: (item) => { state.picked = item.id; ctx.rerender(); } }));
  };

  const view = (ctx) => {
    const { kit } = ctx, { el } = kit;
    const uri = opened(ctx);
    if (uri && state.reading !== uri && ctx.host.can("openai/resource")) readOpened(ctx, uri);
    const shown = current(ctx);
    if (!shown || !shown.file) return kit.empty(shown ? shown.error || "Opening the file…" : "Open a prefab, an atlas or key bindings from your files, or pick one in Index.");
    const { file } = shown;
    const picked = state.picked && (file.entities || file.images || []).find((x) => (x.where || x.path) === state.picked);
    const what = picked ? (picked.region ? ` (${file.path}#${picked.region})` : ` (${picked.name})`) : "";
    ctx.share(`The person is looking at the ${file.kind} ${file.path}${what} in Journeyman Studio.`, { file: file.path, kind: file.kind });
    const readOnly = file.kind === "bindings" && !(state.saved && state.saved.writable)
      ? (uri ? (state.saved ? `Read only: ${state.saved.why}.` : "Reading the file…") : "Read only here: open the file from your files in ChatGPT to change it.") : "";
    return el("div", {},
      el("div", { class: "head" }, el("h1", {}, file.path.split("/").pop()), el("span", { class: "badge" }, file.kind),
        el("span", { class: "meta" }, `${file.path} · ${file.game}`), el("span", { class: "spacer" }), el("span", { class: "meta" }, state.notice || readOnly),
        el("button", { onclick: () => ctx.host.message(`About ${file.path}: `, { send: false }) }, "Ask about it")),
      file.kind === "bindings" ? bindings(ctx, file) : file.kind === "prefab" ? prefab(ctx, file) : atlas(ctx, file, shown.thumbs));
  };

  return { id: "file", title: "File", order: 35, home: true, render: view };
})());
