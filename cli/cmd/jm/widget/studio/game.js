// Overview: the open game at a glance beside the editor: open it there, play
// it, export it; its scenes, its recorded plays and the moments in them
// (what the person picked), each a way into its tab or a chat.
studioTabs.push((() => {
  "use strict";
  // moment: the one shown ({ game, key, r: play_moment's answer, or error }).
  const state = { picked: null, folded: new Set(), moment: null };

  // A moment: its picture with the picks outlined, from play_moment.
  const showMoment = async (ctx, game, play, n) => {
    const key = `${play}#${n}`, asked = {};  // this ask: a later one (another moment, or game) wins
    state.moment = { game, key, asked };
    ctx.rerender();
    let shown;
    try {
      const r = await ctx.call("play_moment", { game, play, marker: "m" + n });  // this game's, whichever is open by then
      shown = r && r.structuredContent && r.structuredContent.marker ? { game, key, r } : { game, key, error: (r && r.content && r.content[0] && r.content[0].text) || "No such moment." };
    } catch (e) { shown = { game, key, error: String(e && e.message || e) }; }
    if (state.moment && state.moment.asked === asked) { state.moment = shown; ctx.rerender(); }
  };
  const momentView = (ctx, m) => {
    const { el } = ctx.kit;
    if (m.error) return el("div", { class: "meta" }, m.error);
    if (!m.r) return el("div", { class: "meta" }, "Looking at that moment…");
    const s = m.r.structuredContent, src = (m.r._meta || {})["jm/image"];
    const title = `Moment ${s.marker.n} of play ${s.play}${s.marker.note ? ": " + s.marker.note : ""}`;
    // Which game, said outright: a new chat (or this one, later) may have another game open.
    const name = ctx.game && ctx.game.path === m.game ? ctx.game.name : m.game.split("/").pop();
    const which = `the game ${name} (${m.game}): open_game "${m.game}" before play_moment or play_state`;
    const text = `In ${which}.\n` + (m.r.content || []).filter((c) => c.type === "text").map((c) => c.text).join("\n");
    return el("div", { class: "moment" },
      src ? el("img", { class: "moment-picture", src, alt: title }) : null,
      el("ol", { class: "moment-picks" }, (s.picked || []).map((line) => el("li", {}, line))),
      ctx.ask({ title, thumb: src, text, question: `Look at what I picked in moment ${s.marker.n} of my play ${s.play} in ${which}: what's going on there?` }));
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


  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    const can = ctx.host.can("callTool");
    const plays = ctx.plays();
    const thumb = ctx.games.thumbs[game.path];
    const picked = state.picked && state.picked.game === game.path ? state.picked : null;
    if (picked) ctx.share(`The person is looking at ${picked.what} of ${game.name} in Journeyman Studio.`, { picked: picked.what },
      { title: picked.what.split("/").pop(), text: `${picked.what}, a scene of ${game.name} (${game.path})` });

    const scenes = kit.section("Scenes", (game.scenes || []).length
      ? kit.tree({ label: "Scenes", items: sceneTree(game.scenes), folded: state.folded, selected: picked && picked.what,
          onPick: (item) => {
            if (!item.scene) return;
            if (ctx.has("editor")) ctx.go(ctx.link("editor", { scene: item.id }));
            else { state.picked = { game: game.path, what: item.id }; ctx.rerender(); }
          } })
      : el("div", { class: "meta" }, "No scenes yet."));
    const playList = kit.section("Recent plays", !plays ? el("div", { class: "meta" }, can ? "Reading the plays…" : "")
      : plays.error ? el("div", { class: "meta" }, plays.error)
      : plays.plays.length ? kit.tree({ label: "Recent plays", items: plays.plays.slice(0, 12).map((p) => ({ id: p.id, title: p.id, label: kit.started(p.started),
          meta: `${kit.duration(p.seconds)}${p.markers ? ` · ${p.markers} marker${p.markers === 1 ? "" : "s"}` : ""}${p.stale ? " · older build" : ""}${p.by === "agent" ? " · an agent's run" : ""}` })),
          onPick: (item) => ctx.reviewPlay(plays.plays.find((p) => p.id === item.id)) })
      : el("div", { class: "meta" }, "No plays yet: Play records one."));
    // The latest moments, newest first (plays come newest first): markers the person picked things in.
    const moments = (plays && plays.plays || []).flatMap((p) => [...(p.moments || [])].reverse().map((n) => ({ play: p.id, n,
      line: (p.marks || []).find((l) => l.startsWith(`m${n} `)) || `m${n}` }))).slice(0, 8);
    const shown = state.moment && state.moment.game === game.path ? state.moment : null;
    const momentList = moments.length ? kit.section("Moments", kit.tree({ label: "Moments", selected: shown && shown.key,
      items: moments.map((m) => ({ id: `${m.play}#${m.n}`, title: `${m.play} ${m.line}`, label: m.line, meta: m.play })),
      onPick: (item) => { const m = moments.find((x) => `${x.play}#${x.n}` === item.id); showMoment(ctx, game.path, m.play, m.n); } }),
      shown ? momentView(ctx, shown) : null) : null;

    // Its folder, short: the whole path is the title.
    const short = game.path.split(/[\\/]/).filter(Boolean).slice(-2).join("/");
    const stat = (n, what) => el("div", { class: "stat" }, el("b", {}, String(n)), el("span", {}, what));
    const count = plays && plays.plays ? plays.plays.length : null, marked = (plays && plays.plays || []).reduce((n, p) => n + (p.moments || []).length, 0);
    return el("div", {},
      el("div", { class: thumb ? "hero" : "hero bare" },
        thumb ? el("img", { class: "hero-thumb", src: thumb, alt: "" }) : null,
        el("div", { class: "hero-text" }, el("h1", {}, game.name),
          el("span", { class: "meta", title: game.path }, `…/${short} · ${game.opened ? "opened " + kit.ago(game.opened) : "never opened"}`)),
        el("div", { class: "hero-actions" },
          can ? ctx.projectActions(game, { primary: true }) : null,
          ctx.ask({ title: game.name, thumb, question: `Let's work on my game ${game.name}.`,
            text: `The game ${game.name} (${game.path}): ${(game.scenes || []).length} scenes. open_game it to work on it.` }))),
      el("div", { class: "stats" }, stat((game.scenes || []).length, "scenes"),
        count !== null ? stat(count, count === 1 ? "play" : "plays") : null,
        count !== null ? stat(marked, marked === 1 ? "moment" : "moments") : null,
        count ? stat(kit.duration(plays.plays[0].seconds), "last play") : null),
      kit.split("overview", scenes, el("div", {}, playList, momentList), { ratio: 0.5 }));
  };

  return { id: "game", title: "Overview", order: 10, render };
})());
