// Games: the person's games as cards (Studio's home): open one here, play
// it, open it in the editor, export it, or make a new one.
studioTabs.push((() => {
  "use strict";
  // selected: a game's path. draft: the new game's name while its form is open (null: closed).
  // looked: games_find ran by itself once (an empty list on first use).
  const state = { selected: null, draft: null, looked: false };

  // Opened even when it's listed as open: another chat may have opened another since.
  const openHere = async (ctx, g) => {
    if (await ctx.act("Opening the game", "open_game", { game: g.path }, () => `${g.name} is open.`)) ctx.go(ctx.link("game"));
  };

  const find = (ctx) => ctx.act("Looking for your games", "games_find", {}, (text) => text || "Looked for your games.");

  const newGame = (ctx) => {
    const { el } = ctx.kit;
    const input = el("input", { placeholder: "The new game's name", "aria-label": "Name", maxlength: "80", "data-key": "new-name",
      oninput: (e) => { state.draft = e.target.value; },
      onkeydown: (e) => { if (e.key === "Escape") { state.draft = null; ctx.rerender(); } } });
    input.value = state.draft;
    return el("form", { class: "head", onsubmit: async (e) => {
      e.preventDefault();
      const name = (state.draft || "").trim();
      if (!name) return;
      state.draft = null;
      if (await ctx.act("Making the game", "new_game", { name }, () => `${name} is made and open.`)) ctx.go(ctx.link("game"));
    } }, input, el("button", { class: "primary", type: "submit", disabled: ctx.busy }, "Create"),
      el("button", { type: "button", onclick: () => { state.draft = null; ctx.rerender(); } }, "Cancel"));
  };

  // The page's own field: a form asked from a page's call isn't shown by every host (ChatGPT's isn't).
  const startNew = (ctx) => { state.draft = ""; ctx.rerender(); ctx.kit.focusKey("new-name"); };

  // Games named alike say where they are: the fewest folders at the end of
  // their paths that tell them apart (Windows' or not), else the whole path.
  const places = (games) => {
    const out = {}, parts = (g) => g.path.split(/[\\/]/).filter(Boolean);
    for (const g of games) {
      const alike = games.filter((x) => x.name === g.name);
      if (alike.length < 2) continue;
      out[g.path] = g.path;
      for (let n = 1; n <= parts(g).length; n++) {
        const tail = (x) => parts(x).slice(-n).join("/");
        if (alike.every((x) => x === g || tail(x) !== tail(g))) { out[g.path] = tail(g); break; }
      }
    }
    return out;
  };

  const render = (ctx) => {
    const { kit } = ctx, { el } = kit;
    // A variant or sandbox opened to work in is its game's, not a game of its own.
    const { open, thumbs } = ctx.games, games = ctx.games.games.filter((g) => !g.copyOf);
    const inCopy = ctx.copy, where = places(games);
    const can = ctx.host.can("callTool");
    if (!games.length && can && !state.looked && !ctx.busy) { state.looked = true; Promise.resolve().then(() => find(ctx)); }
    const picked = games.find((g) => g.path === state.selected);
    if (picked) {
      ctx.share(`The person picked the game ${picked.name} (${picked.path}) in Journeyman Studio. open_game it to work on it.`,
        { picked: { game: picked.name, path: picked.path, scenes: picked.scenes || [], opened: picked.opened || 0 } },
        { title: picked.name, thumb: thumbs[picked.path], text: `The game ${picked.name} (${picked.path}). open_game it to work on it.` });
    }
    const actions = (item) => {
      const g = games.find((x) => x.path === item.id), arg = { game: g.path };
      if (g.missing || !can) return [];
      return [
        el("button", { class: "primary", disabled: ctx.busy, onclick: () => openHere(ctx, g) }, g.path === open ? "Show" : "Open"),
        ...ctx.projectActions(g),
      ];
    };
    return el("div", {},
      el("div", { class: "head" }, el("h1", {}, "Your games"), el("span", { class: "meta" }, String(games.length)), el("span", { class: "spacer" }),
        can ? el("button", { disabled: ctx.busy, title: "Look through Documents, Desktop and Developer for games", onclick: () => find(ctx) }, "Find my games") : null,
        can && state.draft === null ? el("button", { class: "primary", onclick: () => startNew(ctx) }, "New game") : null),
      state.draft !== null ? newGame(ctx) : null,
      games.length ? kit.grid({ label: "Your games", selected: state.selected,
        items: games.map((g) => ({ id: g.path, title: g.name || g.path, thumb: thumbs[g.path],
          meta: `${where[g.path] ? where[g.path] + " · " : ""}${g.opened ? "opened " + kit.ago(g.opened) : "never opened"} · ${(g.scenes || []).length} scene${(g.scenes || []).length === 1 ? "" : "s"}`,
          badges: [g.path === open && { text: "open" }, inCopy && inCopy.game === g.path && { text: "in " + inCopy.name }, g.missing && { text: "missing", warn: true }].filter(Boolean) })),
        onPick: (item) => { state.selected = item.id; ctx.rerender(); },
        onOpen: (item) => { const g = games.find((x) => x.path === item.id); if (!g.missing && can) openHere(ctx, g); },
        actions }) : kit.empty("No games yet: make one with New game, or Find my games."));
  };

  return { id: "games", title: "Games", order: 0, home: true, render };
})());
