// Overview: the open game at a glance beside the editor: open it there, play
// it, export it; its scenes, its recorded plays and the moments in them
// (what the person picked), each a way into its tab or a chat.
studioTabs.push((() => {
  "use strict";
  // plays: the open game's plays_list answer ({ path, plays } or { path, error }), asked again each visit.
  // moment: the one shown ({ game, key, r: play_moment's answer, or error }).
  const state = { plays: null, asking: "", picked: null, folded: new Set(), moment: null };

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

  const askPlays = async (ctx, path) => {
    const asked = state.asking;  // this game and visit: a later ask (another game, a new visit) wins
    let plays;
    try {
      const r = await ctx.call("plays_list", { game: path });  // this game's, whichever is open by then
      const s = r && r.structuredContent;
      if (s && s.game && s.game !== path) return;  // about another game
      plays = s && Array.isArray(s.plays) ? { path, plays: s.plays } : { path, error: (r && r.content && r.content[0] && r.content[0].text) || "No plays." };
    } catch (e) { plays = { path, error: String(e && e.message || e) }; }
    if (state.asking !== asked) return;
    state.plays = plays;
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

  // To the composer, to ask about it (play_show opens its review).
  const openPlay = (ctx, p) => {
    ctx.host.message(`Let's look at my play ${p.id} (${length(p.seconds)} long, ${p.markers} marker${p.markers === 1 ? "" : "s"}).`, { send: false });
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    const can = ctx.host.can("callTool");
    if (can && state.asking !== game.path + "#" + ctx.visit) { state.asking = game.path + "#" + ctx.visit; askPlays(ctx, game.path); }
    const thumb = ctx.games.thumbs[game.path];
    const plays = state.plays && state.plays.path === game.path ? state.plays : null;
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
      : plays.plays.length ? kit.tree({ label: "Recent plays", items: plays.plays.slice(0, 12).map((p) => ({ id: p.id, title: p.id, label: when(p.started),
          meta: `${length(p.seconds)}${p.markers ? ` · ${p.markers} marker${p.markers === 1 ? "" : "s"}` : ""}${p.stale ? " · older build" : ""}` })),
          onPick: (item) => openPlay(ctx, plays.plays.find((p) => p.id === item.id)) })
      : el("div", { class: "meta" }, "No plays yet: Play records one."));
    // The latest moments, newest first (plays come newest first): markers the person picked things in.
    const moments = (plays && plays.plays || []).flatMap((p) => [...(p.moments || [])].reverse().map((n) => ({ play: p.id, n,
      line: (p.marks || []).find((l) => l.startsWith(`m${n} `)) || `m${n}` }))).slice(0, 8);
    const shown = state.moment && state.moment.game === game.path ? state.moment : null;
    const momentList = moments.length ? kit.section("Moments", kit.tree({ label: "Moments", selected: shown && shown.key,
      items: moments.map((m) => ({ id: `${m.play}#${m.n}`, title: `${m.play} ${m.line}`, label: m.line, meta: m.play })),
      onPick: (item) => { const m = moments.find((x) => `${x.play}#${x.n}` === item.id); showMoment(ctx, game.path, m.play, m.n); } }),
      shown ? momentView(ctx, shown) : null) : null;

    return el("div", {},
      el("div", { class: thumb ? "hero" : "hero bare" },
        thumb ? el("img", { class: "hero-thumb", src: thumb, alt: "" }) : null,
        el("div", { class: "hero-text" }, el("h1", {}, game.name),
          el("span", { class: "meta", title: game.path }, `${game.path} · ${game.opened ? "opened " + kit.ago(game.opened) : "never opened"}`)),
        el("div", { class: "hero-actions" },
          can ? el("div", {}, ctx.projectActions(game, { primary: true })) : null,
          el("div", {}, ctx.ask({ title: game.name, thumb, question: `Let's work on my game ${game.name}.`,
            text: `The game ${game.name} (${game.path}): ${(game.scenes || []).length} scenes. open_game it to work on it.` })))),
      kit.split("overview", scenes, el("div", {}, playList, momentList), { ratio: 0.5 }));
  };

  return { id: "game", title: "Overview", order: 10, render };
})());
