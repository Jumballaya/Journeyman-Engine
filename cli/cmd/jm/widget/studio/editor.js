// Editor: the bridge to the editor on the person's screen. What they have
// open and selected there, the notes they point out to their agent (which
// come to the chat as drafts), and "show me": a scene or an entity picked
// here is opened, selected and framed there.
studioTabs.push((() => {
  "use strict";
  // editor: the latest editor_state ({ editors } or null before one). notes: note
  // keys seen ("game:pid:id"); known: games whose notes were read once (those older
  // than the Studio aren't drafted). scenes: the game's scene_view answers by scene
  // path, asked again each visit; renders: its scene_render pictures ({ src } |
  // { error } | { loading }). scene: the scene picked to show from. game: whose
  // all this is: another game's clears it, and an answer asked for one no longer shown is dropped.
  const state = { editor: null, error: "", notes: new Set(), known: new Set(), ctx: null, polling: false, game: null, linked: undefined,
                  scenes: {}, renders: {}, scene: null };

  const keyOf = (e, n) => `${e.pid}:${n.id}`;
  const gameOf = (ctx) => (ctx.game ? ctx.game.path : null);
  // The game shown now; a new one starts with nothing of the last one's.
  const forGame = (ctx) => {
    const game = gameOf(ctx);
    if (state.game !== game) Object.assign(state, { game, editor: null, error: "", scenes: {}, renders: {}, scene: null });
    return game;
  };
  const about = (n) => (n.entities && n.entities.length ? `${n.entities.join(", ")} in ${n.scene}` : n.scene);

  // Polls the editor while the Studio is open (every 2 s, only when the page is seen).
  const poll = async () => {
    const ctx = state.ctx;
    if (document.visibilityState !== "hidden") {
      const game = forGame(ctx);
      const r = await kit.answer(() => ctx.call("editor_state", {}));
      const editors = !r.isError && r.structuredContent && r.structuredContent.editors;
      if (forGame(state.ctx) === game && !Array.isArray(editors)) state.error = kit.said(r) || "no answer";
      else if (forGame(state.ctx) === game) {  // else: asked for a game no longer shown
        const before = JSON.stringify(state.editor);
        state.editor = { editors };
        state.error = "";
        draftNew(ctx, game, editors);
        if (JSON.stringify(state.editor) !== before) ctx.rerender();
      }
    }
    setTimeout(poll, 2000);
  };

  // A note the person just pointed out goes to the chat's composer, unsent (hosts that can draft only).
  const draftNew = (ctx, game, editors) => {
    const first = !state.known.has(game);
    state.known.add(game);
    for (const e of editors) for (const n of e.notes || []) {
      const key = `${game}:${keyOf(e, n)}`;
      if (state.notes.has(key)) continue;
      state.notes.add(key);
      if (!first && ctx.host.can("openai/message")) ctx.host.message(`About ${about(n)}: ${n.text}`, { send: false });
    }
  };

  const show = (ctx, args, label) =>
    ctx.act("Showing it in the editor", "editor_show", args, () => `${label} is shown in the editor.`);

  const sceneAsks = {};  // scene_view's, a channel by scene
  const sceneEntities = (ctx, scene) => {
    forGame(ctx);  // another game's scenes start over
    const known = state.scenes[scene];
    if (!known || known.visit !== ctx.visit) {
      const visit = ctx.visit;
      state.scenes[scene] = { ...known, visit };  // the last answer shows meanwhile
      const ask = sceneAsks[scene] ||= kit.latest(() => state.game);  // an earlier visit's answer, or another game's, is dropped
      ask(() => ctx.call("scene_view", { scene })).then((r) => {
        if (!r) return;
        const s = !r.isError && r.structuredContent;
        state.scenes[scene] = { ...(s && Array.isArray(s.entities) ? { entities: s.entities } : { error: "Couldn't read it." }), visit };
        ctx.rerender();
      });
    }
    return state.scenes[scene];
  };

  // The engine's picture of scene, drawn on request (it builds and runs the game headless).
  const renderAsks = {};  // scene_render's, a channel by scene
  const render = async (ctx, scene) => {
    forGame(ctx);
    state.renders[scene] = { loading: true };
    ctx.rerender();
    const ask = renderAsks[scene] ||= kit.latest(() => state.game);  // an earlier render's picture, or another game's, is dropped
    const r = await ask(() => ctx.call("scene_render", { scene }));
    if (!r) return;
    const src = !r.isError && r._meta && r._meta["jm/image"];
    state.renders[scene] = src ? { src } : { error: kit.said(r) || "No picture." };
    ctx.rerender();
  };

  const live = (ctx, e) => {
    const { kit } = ctx, { el } = kit;
    const notes = (e.notes || []).slice().reverse();
    return [
      kit.section("Open there", el("div", { class: "name" }, el("span", {}, e.scene ? e.scene.split("/").pop().replace(/\.scene\.json$/, "") : "No scene open"),
          (e.unsaved || []).includes(e.scene) ? el("span", { class: "badge" }, "unsaved") : null),
        e.scene ? el("div", { class: "meta" }, e.scene) : null,
        e.selection && e.selection.length ? el("div", {}, "Selected: ", el("b", {}, e.selection.join(", "))) : el("div", { class: "meta" }, "Nothing selected.")),
      kit.section(`Pointed out (${notes.length})`, notes.length
        ? kit.tree({ label: "Pointed out", wrap: true, items: notes.map((n) => ({ id: keyOf(e, n), label: n.text, meta: `${about(n)} · ${kit.ago(n.at)}` })),
            onPick: (item) => {
              const n = notes.find((x) => keyOf(e, x) === item.id);
              ctx.host.message(`About ${about(n)}: ${n.text}`, { send: false });
            } })
        : el("div", { class: "meta" }, "Select something in the editor and press Ctrl+Shift+M to point it out to your agent.")),
    ];
  };

  const picture = (ctx, r) => {
    const { el } = ctx.kit;
    if (!r) return null;
    if (r.src) return el("img", { src: r.src, alt: "The scene as the engine draws it", style: { maxWidth: "100%", maxHeight: "360px", display: "block", margin: "0 auto 8px", borderRadius: "6px", imageRendering: "pixelated" } });
    return el("div", { class: "meta" }, r.loading ? "Rendering (building first if it needs it)…" : r.error);
  };

  const showPane = (ctx, open) => {
    const { kit } = ctx, { el } = kit;
    const scenes = ctx.game.scenes || [];
    if (ctx.params.scene !== state.linked) { state.linked = ctx.params.scene; state.scene = null; }  // a new link picks again
    const scene = [state.scene, ctx.params.scene, open, scenes[0]].find((s) => s && scenes.includes(s));
    if (!scene) return kit.section("Show in the editor", el("div", { class: "meta" }, "No scenes yet."));
    const known = sceneEntities(ctx, scene);
    return kit.section("Show in the editor",
      el("div", { class: "head" },
        el("select", { "aria-label": "Scene", onchange: (ev) => { state.scene = scenes[ev.target.selectedIndex]; ctx.rerender(); } },
          scenes.map((s) => el("option", { selected: s === scene }, s))),
        el("button", { disabled: ctx.busy, onclick: () => show(ctx, { scene }, scene) }, "Show scene"),
        el("button", { disabled: !!(state.renders[scene] || {}).loading, onclick: () => render(ctx, scene) }, "Render")),
      picture(ctx, state.renders[scene]),
      known.entities ? kit.tree({ label: "Entities", items: known.entities.filter((x) => !x.inherited).map((x) => ({ id: x.where, label: x.name || x.where,
          meta: x.prefab ? x.prefab.split("/").pop().replace(/\.prefab\.json$/, "") : "" })),
          onPick: (item) => { if (!ctx.busy) show(ctx, { scene, entity: item.label }, item.label); } })
        : el("div", { class: "meta" }, known.error || "Reading the scene…"),
      el("div", { class: "meta", style: { marginTop: "6px" } }, "Pick one: the editor selects and frames it."));
  };

  const view = (ctx) => {
    forGame(ctx);
    const { kit } = ctx, { el } = kit;
    const editors = state.editor ? state.editor.editors : null;
    const e = editors && editors[0];
    if (e) {
      ctx.share(`The person's editor has ${e.scene || "no scene"} open${e.selection && e.selection.length ? `, ${e.selection.join(", ")} selected` : ""}.`,
        { editor: { scene: e.scene, selection: e.selection || [], notes: (e.notes || []).slice(-3) } });
    }
    const head = el("div", { class: "head" }, el("h1", {}, "Editor"),
      el("span", { class: "meta" }, !editors ? state.error || "Looking for the editor…" : e ? "open on your screen" : "not open"),
      el("span", { class: "spacer" }),
      editors && !e ? el("button", { class: "primary", disabled: ctx.busy,
        onclick: () => ctx.act("Opening the editor", "project_open_editor", {}, () => "The editor is opening.") }, "Open in editor") : null);
    return el("div", {}, head,
      kit.split("editor", el("div", { class: "pane" }, e ? live(ctx, e) : kit.empty(editors ? "Open the editor to see what's open there and point things out." : "")),
        showPane(ctx, e && e.scene), { ratio: 0.45 }));
  };

  // Runs on every Studio page with a game: notes reach the chat whichever tab is shown.
  const background = (ctx) => {
    state.ctx = ctx;
    if (!state.polling && ctx.host.can("callTool")) { state.polling = true; poll(); }
  };

  return { id: "editor", title: "Editor", order: 15, render: view, background };
})());
