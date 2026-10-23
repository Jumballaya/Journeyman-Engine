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
  // gen: bumped when the run followed is set or dropped: an answer asked before is stale. edits: session values typed, not yet set.
  const fresh = (game) => ({ live: undefined, watches: [], adding: "", profile: null, profiling: false, mark: "", polls: 0, game, run: "", gen: 0, edits: {}, speedAsked: undefined });
  let state = { ...fresh(null), ctx: null, polling: false };
  const forGame = (ctx) => {
    if (ctx.game && state.game !== ctx.game.path) state = { ...state, ...fresh(ctx.game.path) };
    return state;
  };
  // The call's arguments for the game shown and the run followed.
  const target = (args) => ({ ...args, game: state.game, ...(state.run ? { run: state.run } : {}) });
  const asking = () => ({ state, gen: state.gen });
  // An answer to keep: asked for the game the Studio still shows (this tab or another), about the run still followed.
  const ours = (asked, r) => {
    const s = r && r.structuredContent, shown = state.ctx && state.ctx.game && state.ctx.game.path;
    if (asked.state !== state || asked.gen !== state.gen || shown !== state.game || (s && s.game && s.game !== state.game)) return false;
    if (s && s.run && s.run !== state.run) {
      if (state.run) return false;  // another run's
      state.run = s.run;  // found
      state.gen++;
    }
    return true;
  };
  // Gone: the next run is found anew. One that was running left a play behind.
  const lose = () => {
    const ended = !!state.live;
    state.live = null; state.speedAsked = undefined;
    if (state.run) { state.run = ""; state.gen++; }
    if (ended && state.ctx) state.ctx.playsChanged();
  };

  const { said } = kit;

  let pollSeq = 0;
  // Asks the game while its tab is shown: its status, the watched values, a picture.
  const poll = async () => {
    const ctx = state.ctx;
    forGame(ctx);
    const mine = asking();
    if (ctx.current === "play" && document.visibilityState !== "hidden") {
      const profile = state.profiling && state.polls++ % 4 === 0;
      const asked = ++pollSeq;
      const watching = state.watches.slice();  // answers are mapped to what was asked
      const commands = ["status", ...watching.map((w) => "get " + w), ...(profile ? ["profile"] : [])];
      try {
        const r = await ctx.call("play_live", target({ commands, frame: true }));
        if (!ours(mine, r)) { setTimeout(poll, 700); return; }
        const answers = r && r.structuredContent && r.structuredContent.answers;
        if (!Array.isArray(answers)) lose();
        else {
          const values = {};
          watching.forEach((w, i) => { if (state.watches.includes(w)) values[w] = answers[i + 1]; });
          if (profile && answers.at(-1) && answers.at(-1).profile) state.profile = answers.at(-1).profile;
          const k = state.speedAsked;  // a poll sent after the ask was answered is the truth
          if (k && ((k.done !== undefined && asked > k.done) || (answers[0] && Math.abs(answers[0].speed - k.x) < 0.01))) state.speedAsked = undefined;
          state.live = { status: answers[0], src: (r._meta && r._meta["jm/image"]) || (state.live && state.live.src), values };
        }
      } catch (e) { if (mine.state === state && mine.gen === state.gen) lose(); }
      // Drawn again whatever has focus (drafts are state); the focused field gets its caret back.
      const at = document.activeElement, key = at && at.getAttribute && at.getAttribute("data-key");
      const caret = at && typeof at.selectionStart === "number" ? [at.selectionStart, at.selectionEnd] : null;
      state.drawing = true;
      try { ctx.rerender(); } finally { state.drawing = false; }
      const now = document.activeElement;
      if (caret && now && now.setSelectionRange && now.getAttribute && now.getAttribute("data-key") === key) now.setSelectionRange(...caret);
    }
    setTimeout(poll, 700);
  };

  const send = async (ctx, command) => {
    forGame(ctx);
    const mine = asking();
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
      el("button", { class: "primary", "data-key": "pause", onclick: () => send(ctx, s.paused ? "resume" : "pause") }, s.paused ? "Resume" : "Pause"),
      el("button", { "data-key": "step", disabled: !s.paused, title: "One frame (while paused)", onclick: () => send(ctx, "step") }, "Step"),
      // A button, not a select: the poll's redraw would close a select's open menu.
      el("button", { "data-key": "speed", title: "Speed (click for the next)", onclick: () => askSpeed(ctx, nextSpeed(shownSpeed(s))) },
        Math.abs(shownSpeed(s) - 1) < 0.01 ? "Full speed" : `${shownSpeed(s)}×`),
      el("span", { class: "meta" }, `${(s.scene || "").split("/").pop().replace(/\.scene\.json$/, "")} · frame ${s.frame}${s.paused ? " · paused" : ""}`),
      el("span", { class: "spacer" }),
      el("input", { placeholder: "What's this moment?", "aria-label": "Marker note", "data-key": "mark-note", value: state.mark,
        oninput: (e) => { state.mark = e.target.value; }, onkeydown: (e) => { if (e.key === "Enter") mark(ctx); } }),
      el("button", { "data-key": "mark", onclick: () => mark(ctx), title: "Mark this moment in the recorded play (as F8 does)" }, "Mark"));
  };

  // A typed draft goes out once, on change or on leaving the field (a redraw's change or blur doesn't count).
  const commit = (ctx, w, key) => {
    if (state.edits[w] === undefined) return;
    const value = state.edits[w];
    delete state.edits[w];
    send(ctx, `set ${key} ${value}`);
  };

  const speeds = [1, 0.5, 0.25, 0.1];
  // An asked speed shows until a poll reports it (or the ask fails), so a stale poll can't undo a click.
  const shownSpeed = (s) => (state.speedAsked ? state.speedAsked.x : s.speed ?? 1);
  const askSpeed = async (ctx, x) => {
    const ask = { x };
    state.speedAsked = ask;
    ctx.rerender();
    const mine = asking();
    let answer = null;
    try { answer = await send(ctx, "speed " + x); } catch (e) {}
    const failed = !answer || answer.ok === false;
    if (!failed) ask.done = pollSeq;
    if (failed && mine.state === state && mine.gen === state.gen && state.speedAsked === ask) { state.speedAsked = undefined; ctx.rerender(); }
  };
  const nextSpeed = (now) => speeds[(speeds.findIndex((x) => Math.abs(x - now) < 0.01) + 1) % speeds.length];

  const watchList = (ctx, values) => {
    const { kit } = ctx, { el } = kit;
    const rows = state.watches.map((w) => {
      const a = values[w] || {};
      const value = a.ok === false ? a.error : JSON.stringify(a.value !== undefined ? a.value : a.values);
      const key = w.startsWith("session.") ? w.slice(8) : null;  // session values can be set
      return el("div", { class: "field" }, el("label", { title: w }, w),
        el("div", { class: "vals" },
          key ? el("input", { value: state.edits[w] ?? value ?? "", "data-key": "watch:" + w, "aria-label": w,
            oninput: (e) => { state.edits[w] = e.target.value; },
            onchange: (e) => { if (!state.drawing) { state.edits[w] = e.target.value; commit(ctx, w, key); } },
            onblur: () => { if (!state.drawing) commit(ctx, w, key); } }) : el("span", { class: a.ok === false ? "meta" : "" }, value || "…"),
          el("button", { "data-key": "unwatch:" + w, title: "Stop watching", onclick: () => { state.watches = state.watches.filter((x) => x !== w); ctx.rerender(); } }, "×")));
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
      el("div", { class: "head" }, el("label", {}, el("input", { type: "checkbox", "data-key": "measure", checked: state.profiling,
        onchange: (e) => { state.profiling = e.target.checked; state.polls = 0; ctx.rerender(); } }), " Measure"),
        p ? el("span", { class: "meta" }, `${p.frame.avgMs} ms a frame on average, ${p.frame.worstMs} at worst`) : null),
      ...parts.map((x) => el("div", { class: "field", title: `${x.kind}: worst ${x.worstMs} ms` }, el("label", {}, x.name),
        el("div", { style: { background: "var(--accent)", height: "8px", borderRadius: "4px", width: `${Math.max(2, 100 * x.avgMs / most)}%` } }))));
  };

  // Nothing running: the way back in. The last play, to talk over with the
  // agent or play on from (its last marker, else its end), or a fresh start.
  const idle = (ctx) => {
    const { kit } = ctx, { el } = kit;
    const start = (label, primary) => el("button", { class: primary ? "primary" : null, "data-key": "start", disabled: ctx.busy,
      onclick: () => ctx.act("Starting the game", "project_play", {}, () => "The game is open in its own window: what you play is recorded.") }, label);
    const answer = ctx.plays(), last = answer && answer.plays && answer.plays.find((p) => p.by !== "agent");  // theirs, not an agent's run
    if (!last) return kit.empty("The game isn't running. Play it in its own window and follow it here.", start("Play", true));
    const marks = last.marks || [];  // "m2 0:07.0: walked into town", in order
    const at = last.markers ? `m${last.markers}` : "";
    const resume = el("button", { class: "primary", "data-key": "resume", disabled: ctx.busy,
      title: "Opens the game at that moment, replayed exactly; what you play from there is recorded as a new play",
      onclick: () => ctx.act("Opening the game there", "play_resume", { play: last.id, ...(at ? { at } : {}) },
        () => "The game is open where you left off: what you play from there is recorded." + (!last.stale ? ""
          : at ? ` The game changed since: if the replay doesn't get you to marker ${last.markers}'s scene, you start there with your values from then.`
          : " The game changed since, so it may not be quite the same spot.")) },
      at ? `Play on from marker ${last.markers}` : "Play on from the end");
    return el("div", { class: "last-play" },
      kit.section("Your last play",
        el("div", { class: "meta" }, `${kit.started(last.started)} · ${kit.duration(last.seconds)}${last.stale ? " · the game changed since" : ""}`),
        marks.length ? el("ol", { class: "marks" }, marks.map((line) => el("li", {}, line))) : el("div", { class: "meta" }, "No markers: F8 marks a moment as you play."),
        el("div", { class: "actions" }, resume,
          el("button", { "data-key": "review", onclick: () => ctx.reviewPlay(last) }, "Talk it over"),
          start("Play from the start", false))));
  };

  const view = (ctx) => {
    forGame(ctx);
    const { kit } = ctx, { el } = kit;
    const live = state.live;
    if (live === undefined) return kit.empty("Looking for the game…");
    if (!live) return idle(ctx);
    const s = live.status || {};
    ctx.share(`The person is playing ${ctx.game.name}: ${s.scene}, frame ${s.frame}${s.paused ? ", paused" : ""}.`, { live: s });
    // Beside the chat (the host's picture-in-picture): the game and its controls only, as the person talks.
    // (Its way back is the Studio's, shown whatever this tab shows.)
    const host = ctx.host, pip = host.displayMode === "pip";
    const bar = controls(ctx, s);
    // A host that doesn't list its modes (ChatGPT's window.openai) is asked anyway: it says no if it can't.
    if (!pip && host.can("displayMode") && (!host.displayModes || host.displayModes.includes("pip"))) bar.append(el("span", { class: "spacer" }),
      el("button", { "data-key": "pip", title: "Keep the game beside the chat while you talk", onclick: () => host.requestDisplayMode("pip").catch(() => {}) }, "Beside the chat"));
    const picture = live.src ? el("img", { src: live.src, alt: "The game now", style: { width: "100%", maxHeight: pip ? "none" : "72vh", objectFit: "contain", background: "#000", borderRadius: "8px", imageRendering: "pixelated", display: "block" } })
      : kit.empty("No picture yet.");
    if (pip) return el("div", {}, bar, picture);
    return el("div", {}, bar,
      kit.split("play", picture,
      el("div", { class: "pane" }, watchList(ctx, live.values || {}), profileStrip(ctx)), { ratio: 0.6 }));
  };

  const background = (ctx) => {
    state.ctx = ctx;
    if (!state.polling && ctx.host.can("callTool")) { state.polling = true; poll(); }
  };

  return { id: "play", title: "Play", order: 20, render: view, background };
})());
