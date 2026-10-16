// Overview: the open game at a glance beside the editor: open it there, play
// it, export it; its scenes and its recorded plays, each a way into its tab.
studioTabs.push((() => {
  "use strict";
  // plays: the open game's plays_list answer ({ path, plays } or { path, error }), asked again each visit.
  const state = { plays: null, asking: "", picked: null, folded: new Set() };

  const askPlays = async (ctx, path) => {
    try {
      const r = await ctx.call("plays_list", {});
      const plays = r && r.structuredContent && r.structuredContent.plays;
      state.plays = Array.isArray(plays) ? { path, plays } : { path, error: (r && r.content && r.content[0] && r.content[0].text) || "No plays." };
    } catch (e) { state.plays = { path, error: String(e && e.message || e) }; }
    ctx.rerender();
  };

  // scenes/levels/a.scene.json → a tree by folder (scenes/ left out when every scene is in it).
  const sceneTree = (scenes) => {
    const root = { children: [] };
    const first = scenes[0].split("/")[0];
    const skip = scenes.every((p) => p.startsWith(first + "/")) ? 1 : 0;  // the folder every scene is in
    for (const path of scenes) {
      let at = root;
      const parts = path.split("/");
      parts.forEach((part, i) => {
        if (i < skip) return;
        const id = parts.slice(0, i + 1).join("/");
        let next = at.children.find((c) => c.id === id);
        if (!next) at.children.push(next = { id, label: part.replace(/\.scene\.json$/, ""), children: [], scene: i === parts.length - 1 });
        at = next;
      });
    }
    return root.children;
  };

  const length = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`;
  const when = (iso) => { const d = new Date(iso); return isNaN(d) ? iso : d.toLocaleString([], { dateStyle: "medium", timeStyle: "short" }); };

  // Into a play's tab when there's one; else the composer, to ask about it.
  const openPlay = (ctx, p) => {
    if (ctx.has("play")) ctx.go(ctx.link("play", { play: p.id }));
    else ctx.host.message(`Let's look at my play ${p.id} (${length(p.seconds)} long, ${p.markers} marker${p.markers === 1 ? "" : "s"}).`, { send: false });
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    const can = ctx.host.can("callTool");
    if (can && state.asking !== game.path + "#" + ctx.visit) { state.asking = game.path + "#" + ctx.visit; askPlays(ctx, game.path); }
    const thumb = ctx.games.thumbs[game.path];
    const plays = state.plays && state.plays.path === game.path ? state.plays : null;
    const picked = state.picked && state.picked.game === game.path ? state.picked : null;
    if (picked) ctx.share(`The person is looking at ${picked.what} of ${game.name} in Journeyman Studio.`, { picked: picked.what });

    const scenes = kit.section("Scenes", (game.scenes || []).length
      ? kit.tree({ label: "Scenes", items: sceneTree(game.scenes), folded: state.folded, selected: picked && picked.what,
          onPick: (item) => {
            if (!item.scene) return;
            if (ctx.has("scene")) ctx.go(ctx.link("scene", { scene: item.id }));
            else { state.picked = { game: game.path, what: item.id }; ctx.rerender(); }
          } })
      : el("div", { class: "meta" }, "No scenes yet."));
    const playList = kit.section("Recent plays", !plays ? el("div", { class: "meta" }, can ? "Reading the plays…" : "")
      : plays.error ? el("div", { class: "meta" }, plays.error)
      : plays.plays.length ? kit.tree({ label: "Recent plays", items: plays.plays.slice(0, 12).map((p) => ({ id: p.id, title: p.id, label: when(p.started),
          meta: `${length(p.seconds)}${p.markers ? ` · ${p.markers} marker${p.markers === 1 ? "" : "s"}` : ""}${p.stale ? " · older build" : ""}` })),
          onPick: (item) => openPlay(ctx, plays.plays.find((p) => p.id === item.id)) })
      : el("div", { class: "meta" }, "No plays yet: Play records one."));

    return el("div", {},
      el("div", { class: "hero" },
        thumb ? el("img", { class: "hero-thumb", src: thumb, alt: "" }) : null,
        el("div", { class: "hero-text" }, el("h1", {}, game.name),
          el("span", { class: "meta", title: game.path }, `${game.path} · ${game.opened ? "opened " + kit.ago(game.opened) : "never opened"}`)),
        can ? el("div", { class: "hero-actions" }, ctx.projectActions(game, { primary: true })) : null),
      kit.split("overview", scenes, playList, { ratio: 0.5 }));
  };

  return { id: "game", title: "Overview", order: 10, render };
})());
