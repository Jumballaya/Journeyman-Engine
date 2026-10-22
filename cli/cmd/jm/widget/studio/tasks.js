// Tasks: the game's task board (design/Tasks.md, jm tasks) as columns. A
// task's links go where they point: a design note, a file, a scene or entity,
// a marker of a recorded play.
studioTabs.push((() => {
  "use strict";
  // board: the tasks answer ({ columns, tasks }). draft: the new task's text. confirm: the remove asked once.
  // failed: why the last listing failed (the board then is the one before).
  const fresh = (game) => ({ game, asking: "", board: null, failed: "", draft: "", links: "", confirm: "", picked: null });
  let state = fresh(null);
  const said = (r) => kit.said(r) || "no answer";
  const asking = kit.latest(() => state);  // the board's: a list or a change; an older answer, or another game's, is dropped

  const load = async (ctx) => {
    const r = await asking(() => ctx.call("tasks", {}));
    if (!r) return;
    state.failed = show(r) ? "" : "Couldn't read the board: " + said(r);
    ctx.rerender();
  };
  const show = (r) => {  // true when r is a board
    const s = r && !r.isError && r.structuredContent;
    if (!s || !Array.isArray(s.tasks)) return false;
    state.board = { columns: s.columns || ["To do", "Doing", "Done"], tasks: s.tasks };
    return true;
  };

  // Changes the board; the answer is the board as it is after.
  const change = async (ctx, name, args) => {
    state.confirm = "";
    const r = await asking(() => ctx.call(name, args));  // a list asked before this is stale; so is this, once another goes
    if (!r) return false;
    if (r && r.isError) ctx.say("Couldn't: " + said(r));
    else if (show(r)) state.failed = "";
    ctx.rerender();
    return !(r && r.isError);
  };

  // A link ("[[Note]]", a jm:// link, else a game file), as a button that goes where it points.
  const link = (ctx, target) => {
    const { el } = ctx.kit;
    let go = null, label = target;
    const note = /^\[\[([^\]|#]+)/.exec(target);
    const play = /^jm:\/\/play\/([^/]+)(?:\/(m\d+))?$/.exec(target);
    const entity = /^jm:\/\/entity\?(.*)$/.exec(target);
    if (play) {
      label = play[2] ? `marker ${play[2].slice(1)} of play ${play[1]}` : `play ${play[1]}`;
      go = () => ctx.host.message(`Let's look at ${label} (${target}).`, { send: false });  // the Play tab is the live game
    } else if (entity) {
      const q = new URLSearchParams(entity[1]);
      label = `${q.get("name")} in ${(q.get("scene") || "").split("/").pop()}`;
      go = () => ctx.go(ctx.has("scene") ? ctx.link("scene", { scene: q.get("scene"), entity: q.get("name") }) : ctx.link("files", { file: q.get("scene") }));
    } else if (note) {
      label = note[1].trim();
      go = () => ctx.go(ctx.link("memory", { note: label }));
    } else go = () => ctx.go(/\.scene\.json$/.test(target) && ctx.has("scene") ? ctx.link("scene", { scene: target }) : ctx.link("files", { file: target }));
    return el("button", { class: "task-link", title: target, onclick: (e) => { e.stopPropagation(); go(); } }, label);
  };

  // The task's text without its links (they're shown as buttons).
  const words = (text) => text.replace(/\s*·\s*(\[\[[^\]]+\]\]|`[^`]+`|jm:\/\/\S+)/g, "").replace(/\[\[([^\]|#]+)[^\]]*\]\]/g, "$1").replace(/`([^`]+)`/g, "$1");

  const card = (ctx, t, columns) => {
    const { el } = ctx.kit, at = columns.indexOf(t.column), chosen = state.picked === t.id;
    const move = (to) => change(ctx, "task_move", { id: t.id, column: columns[to] });
    const pick = () => { state.picked = t.id; ctx.rerender(); };
    return el("div", { class: "task" + (chosen ? " on" : "") + (t.column === "Done" ? " done" : ""), tabindex: "0", "data-key": "task:" + t.id,
      onclick: pick,
      onkeydown: (e) => {  // Enter/Space pick it; ⇧← ⇧→ move it a column
        if (e.target === e.currentTarget && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); pick(); }
        if (e.shiftKey && e.key === "ArrowLeft" && at > 0) { e.preventDefault(); move(at - 1); }
        if (e.shiftKey && e.key === "ArrowRight" && at < columns.length - 1) { e.preventDefault(); move(at + 1); }
      } },
      el("div", { class: "task-text" }, words(t.text)),
      t.links.length ? el("div", { class: "task-links" }, t.links.map((l) => link(ctx, l))) : null,
      (t.notes || []).length ? el("div", { class: "meta task-notes" }, t.notes.map((n) => n.trim().replace(/^- /, "")).join(" · ")) : null,
      el("div", { class: "task-foot" }, el("span", { class: "meta" }, t.id), el("span", { class: "spacer" }),
        at > 0 ? el("button", { title: "Back to " + columns[at - 1], "aria-label": "Back to " + columns[at - 1], onclick: (e) => { e.stopPropagation(); move(at - 1); } }, "←") : null,
        at < columns.length - 1 ? el("button", { title: "On to " + columns[at + 1], "aria-label": "On to " + columns[at + 1], onclick: (e) => { e.stopPropagation(); move(at + 1); } }, "→") : null,
        chosen ? el("button", { onclick: (e) => { e.stopPropagation(); ctx.host.message([ctx.host.text(`About task ${t.id} (“${words(t.text)}”) of ${ctx.game.name}: `)], { send: false }); } }, "Ask") : null,
        chosen ? el("button", { "data-key": "remove:" + t.id, onclick: (e) => {
          e.stopPropagation();
          if (state.confirm === t.id) change(ctx, "task_remove", { id: t.id }); else { state.confirm = t.id; ctx.rerender(); }
        } }, state.confirm === t.id ? "Remove?" : "Remove") : null));
  };

  const styled = (el) => {
    if (styled.done) return;
    styled.done = true;
    document.head.append(el("style", {}, `
      .tasks-board { display: grid; grid-template-columns: repeat(var(--columns), minmax(0, 1fr)); gap: 12px; align-items: start; }
      @media (max-width: 700px) { .tasks-board { grid-template-columns: 1fr; } }
      .tasks-col { display: grid; gap: 8px; align-content: start; min-height: 80px; }
      .task { background: var(--bg); border: 1px solid var(--line); border-radius: var(--radius); padding: 8px 10px; display: grid; gap: 6px; cursor: default; }
      .task.on { border-color: var(--accent); } .task.done .task-text { color: var(--muted); text-decoration: line-through; }
      .task-links { display: flex; flex-wrap: wrap; gap: 4px; }
      .task-link { all: unset; cursor: pointer; font-size: 11px; padding: 1px 7px; border-radius: 999px; background: var(--accent-soft); color: var(--accent);
        max-width: 100%; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
      .task-link:focus-visible { outline: 2px solid var(--accent); }
      .task-foot { display: flex; gap: 4px; align-items: center; } .task-foot button { padding: 1px 8px; font-size: 12px; }
      .tasks-grow { flex: 1; min-width: 160px; } .task-notes { font-size: 12px; }`));
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    styled(el);
    if (!ctx.host.can("callTool")) return kit.empty("This host can't run Journeyman's tools here.");
    if (state.game !== game.path) state = fresh(game.path);
    if (state.asking !== String(ctx.visit)) { state.asking = String(ctx.visit); load(ctx); }
    const retry = state.failed ? el("div", { class: "meta" }, `${state.failed}${state.board ? " (this board may be out of date)" : ""} `,
      el("button", { onclick: () => load(ctx) }, "Try again")) : null;
    if (!state.board) return retry ? el("div", {}, retry) : kit.empty("Reading the board…");
    const { columns, tasks } = state.board;
    const picked = tasks.find((t) => t.id === state.picked);
    ctx.share(picked ? `The person is looking at task ${picked.id} of ${game.name}: ${picked.text}` : `The person is looking at ${game.name}'s task board.`,
      picked ? { task: picked.id, text: picked.text, links: picked.links } : { tasks: tasks.length });
    const text = el("input", { placeholder: "Something to do, e.g. The pit in 1-1 is unfair", "aria-label": "New task", "data-key": "task-new", class: "tasks-grow",
      oninput: (e) => { state.draft = e.target.value; } });
    text.value = state.draft;
    const links = el("input", { placeholder: "Links: Levels, scenes/main.scene.json…", "aria-label": "Its links, comma separated", "data-key": "task-links", class: "tasks-grow",
      oninput: (e) => { state.links = e.target.value; } });
    links.value = state.links;
    const add = el("form", { class: "head", onsubmit: async (e) => {
      e.preventDefault();
      const t = state.draft.trim();
      if (t && await change(ctx, "task_add", { text: t, links: state.links.split(",").map((x) => x.trim()).filter(Boolean) })) { state.draft = ""; state.links = ""; ctx.rerender(); }
    } }, el("h1", {}, "Tasks"), text, links, el("button", { class: "primary", type: "submit" }, "Add"));
    return el("div", {}, add, retry,
      el("div", { class: "tasks-board", style: `--columns: ${columns.length}` }, columns.map((c) => {
        const mine = tasks.filter((t) => t.column === c);
        return kit.section(`${c} · ${mine.length}`, el("div", { class: "tasks-col" }, mine.length ? mine.map((t) => card(ctx, t, columns)) : el("div", { class: "meta" }, "Nothing here.")));
      })));
  };

  return { id: "tasks", title: "Tasks", order: 75, render };
})());
