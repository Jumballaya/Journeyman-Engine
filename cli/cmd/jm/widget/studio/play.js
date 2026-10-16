// Play: a companion to the game the person plays in its own window. It
// follows the game (a picture a moment), pauses, steps and slows it, watches
// values (and sets session ones), shows where frame time goes, and marks
// moments with a note that comes to the chat.
studioTabs.push((() => {
  "use strict";
  // live: the game's last answer ({ status, src, values }), null while none runs,
  // undefined before the first. watches: get paths ("session.score",
  // "tag=Player TransformComponent.x"). profiling: the profile strip is on.
  // game, run: whose run this follows; another game starts over, and an answer about another is dropped.
  const fresh = (game) => ({ live: undefined, watches: [], adding: "", profile: null, profiling: false, mark: "", polls: 0, game, run: "" });
  let state = { ...fresh(null), ctx: null, polling: false };
  const forGame = (ctx) => {
    if (ctx.game && state.game !== ctx.game.path) state = { ...state, ...fresh(ctx.game.path) };
    return state;
  };
  // The call's arguments for the game shown and the run followed.
  const target = (args) => ({ ...args, game: state.game, ...(state.run ? { run: state.run } : {}) });
  // An answer to keep: asked for the game still shown, about its run.
  const ours = (mine, r) => {
    const s = r && r.structuredContent;
    if (mine !== state || (s && s.game && s.game !== state.game)) return false;
    if (s && s.run) state.run = s.run;
    return true;
  };

  const said = (r) => ((r && r.content) || []).filter((c) => c.type === "text").map((c) => c.text).join(" ");

  // Asks the game while its tab is shown: its status, the watched values, a picture.
  const poll = async () => {
    const ctx = state.ctx;
    const mine = forGame(ctx);
    if (ctx.current === "play" && document.visibilityState !== "hidden") {
      const profile = state.profiling && state.polls++ % 4 === 0;
      const watching = state.watches.slice();  // answers are mapped to what was asked
      const commands = ["status", ...watching.map((w) => "get " + w), ...(profile ? ["profile"] : [])];
      try {
        const r = await ctx.call("play_live", target({ commands, frame: true }));
        if (!ours(mine, r)) { setTimeout(poll, 700); return; }
        const answers = r && r.structuredContent && r.structuredContent.answers;
        if (!Array.isArray(answers)) { state.live = null; state.run = ""; }  // gone: the next run is found anew
        else {
          const values = {};
          watching.forEach((w, i) => { if (state.watches.includes(w)) values[w] = answers[i + 1]; });
          if (profile && answers.at(-1) && answers.at(-1).profile) state.profile = answers.at(-1).profile;
          state.live = { status: answers[0], src: (r._meta && r._meta["jm/image"]) || (state.live && state.live.src), values };
        }
      } catch (e) { if (mine === state) { state.live = null; state.run = ""; } }
      const typing = document.activeElement && /^(INPUT|SELECT)$/.test(document.activeElement.tagName || "");
      if (!typing) ctx.rerender();  // a draft in a field stays: the next answer shows

    }
    setTimeout(poll, 700);
  };

  const send = async (ctx, command) => {
    const mine = forGame(ctx);
    const r = await ctx.call("play_live", target({ command }));
    if (!ours(mine, r)) return null;
    const answer = r && r.structuredContent && r.structuredContent.answers && r.structuredContent.answers[0];
    if (!answer || answer.ok === false) ctx.say(`Couldn't: ${(answer && answer.error) || said(r) || "no answer"}`);
    return answer;
  };

  const mark = async (ctx) => {
    const note = state.mark.trim();
    const answer = await send(ctx, ("marker " + note).trim());
    if (!answer || !answer.marker) return;
    state.mark = "";
    ctx.say(`Marked m${answer.marker}.`);
    if (ctx.host.can("openai/message")) {
      ctx.host.message(`In my play just now (marker m${answer.marker})${note ? ": " + note : ""}`, { send: false });
    }
  };

  const controls = (ctx, s) => {
    const { el } = ctx.kit;
    return el("div", { class: "head" },
      el("button", { class: "primary", onclick: () => send(ctx, s.paused ? "resume" : "pause") }, s.paused ? "Resume" : "Pause"),
      el("button", { disabled: !s.paused, title: "One frame (while paused)", onclick: () => send(ctx, "step") }, "Step"),
      el("select", { "aria-label": "Speed", onchange: (e) => send(ctx, "speed " + e.target.value) },
        [1, 0.5, 0.25, 0.1].map((x) => el("option", { value: String(x), selected: Math.abs(x - s.speed) < 0.01 }, x === 1 ? "Full speed" : `${x}×`))),
      el("span", { class: "meta" }, `${(s.scene || "").split("/").pop().replace(/\.scene\.json$/, "")} · frame ${s.frame}${s.paused ? " · paused" : ""}`),
      el("span", { class: "spacer" }),
      el("input", { placeholder: "What's this moment?", "aria-label": "Marker note", "data-key": "mark-note", value: state.mark,
        oninput: (e) => { state.mark = e.target.value; }, onkeydown: (e) => { if (e.key === "Enter") mark(ctx); } }),
      el("button", { onclick: () => mark(ctx), title: "Mark this moment in the recorded play (as F8 does)" }, "Mark"));
  };

  const watchList = (ctx, values) => {
    const { kit } = ctx, { el } = kit;
    const rows = state.watches.map((w) => {
      const a = values[w] || {};
      const value = a.ok === false ? a.error : JSON.stringify(a.value !== undefined ? a.value : a.values);
      const key = w.startsWith("session.") ? w.slice(8) : null;  // session values can be set
      return el("div", { class: "field" }, el("label", { title: w }, w),
        el("div", { class: "vals" },
          key ? el("input", { value: value || "", "data-key": "watch:" + w, "aria-label": w,
            onchange: (e) => send(ctx, `set ${key} ${e.target.value}`) }) : el("span", { class: a.ok === false ? "meta" : "" }, value || "…"),
          el("button", { title: "Stop watching", onclick: () => { state.watches = state.watches.filter((x) => x !== w); ctx.rerender(); } }, "×")));
    });
    const add = el("input", { placeholder: "session.score, tag=Player TransformComponent.x", "aria-label": "Watch a value", "data-key": "watch-add",
      value: state.adding, oninput: (e) => { state.adding = e.target.value; },
      onkeydown: (e) => {
        const w = e.target.value.trim();
        if (e.key === "Enter" && w && !state.watches.includes(w)) { state.watches.push(w); state.adding = ""; ctx.rerender(); kit.focusKey("watch-add"); }
      } });
    return kit.section("Watch", el("div", { class: "form" }, rows, add));
  };

  const profileStrip = (ctx) => {
    const { kit } = ctx, { el } = kit;
    const p = state.profile;
    const parts = p ? ["systems", "scripts", "modules"].flatMap((k) => (p[k] || []).map((x) => ({ ...x, kind: k })))
      .sort((a, b) => b.avgMs - a.avgMs).slice(0, 6) : [];
    const most = Math.max(...parts.map((x) => x.avgMs), 0.01);
    return kit.section("Frame time",
      el("div", { class: "head" }, el("label", {}, el("input", { type: "checkbox", checked: state.profiling,
        onchange: (e) => { state.profiling = e.target.checked; state.polls = 0; ctx.rerender(); } }), " Measure"),
        p ? el("span", { class: "meta" }, `${p.frame.avgMs} ms a frame on average, ${p.frame.worstMs} at worst`) : null),
      ...parts.map((x) => el("div", { class: "field", title: `${x.kind}: worst ${x.worstMs} ms` }, el("label", {}, x.name),
        el("div", { style: { background: "var(--accent)", height: "8px", borderRadius: "4px", width: `${Math.max(2, 100 * x.avgMs / most)}%` } }))));
  };

  const view = (ctx) => {
    forGame(ctx);
    const { kit } = ctx, { el } = kit;
    const live = state.live;
    if (live === undefined) return kit.empty("Looking for the game…");
    if (!live) {
      return kit.empty("The game isn't running. Play it in its own window and follow it here.",
        el("button", { class: "primary", disabled: ctx.busy, onclick: () => ctx.act("Starting the game", "project_play", { game: ctx.game.path },
          () => "The game is open in its own window: what you play is recorded.") }, "Play"));
    }
    const s = live.status || {};
    ctx.share(`The person is playing ${ctx.game.name}: ${s.scene}, frame ${s.frame}${s.paused ? ", paused" : ""}.`, { live: s });
    return el("div", {}, controls(ctx, s),
      kit.split("play", live.src ? el("img", { src: live.src, alt: "The game now", style: { width: "100%", maxHeight: "72vh", objectFit: "contain", background: "#000", borderRadius: "8px", imageRendering: "pixelated", display: "block" } })
        : kit.empty("No picture yet."),
      el("div", { class: "pane" }, watchList(ctx, live.values || {}), profileStrip(ctx)), { ratio: 0.6 }));
  };

  const background = (ctx) => {
    state.ctx = ctx;
    if (!state.polling && ctx.host.can("callTool")) { state.polling = true; poll(); }
  };

  return { id: "play", title: "Play", order: 20, render: view, background };
})());
