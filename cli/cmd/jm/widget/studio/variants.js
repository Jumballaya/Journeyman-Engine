// Variants: other takes on the game (jm variant) and the agent's throwaway
// sandboxes (jm sandbox). A variant is compared with the game frame beside
// frame, played, then merged or dropped; a sandbox that worked becomes one.
studioTabs.push((() => {
  "use strict";
  // variants / sandboxes: their tools' answers. picked: { kind: "variant" | "sandbox", name }.
  // compared: { name, kind, play, at, result | null } while/after comparing. play / at: the moment picked to compare at
  // (null: the default, the person's latest play at its last marker); play "none": no inputs, at frame. confirm: the button asked once.
  // failed: { variants, sandboxes }: why the last ask for that list failed (its data then is the one before).
  const fresh = (game) => ({ game, asking: "", variants: null, thumbs: {}, sandboxes: null, sandboxThumbs: {}, failed: {}, picked: null, compared: null,
                             frame: 120, play: null, at: null, draft: null, newName: "", confirm: "", routed: null, ran: Object.create(null) });  // ran: by sandbox name (any name, "constructor" too)
  let state = fresh(null);

  const { said } = kit;
  const loading = kit.latest(() => state);  // an older answer, or another game's, is dropped
  // A call's arguments naming the game shown: another caller opening a game meanwhile can't redirect it.

  const load = async (ctx) => {
    const all = await loading(() => Promise.all(["variants", "sandboxes"].map((tool) => kit.answer(() => ctx.call(tool, {})))));
    if (!all) return;
    const [v, s] = all;
    const list = (key, r) => {  // a failed ask keeps what was there, said to be failing
      const items = r && !r.isError && r.structuredContent && r.structuredContent[key];
      if (Array.isArray(items)) { state[key] = items; delete state.failed[key]; } else state.failed[key] = said(r) || `Couldn't list the ${key}.`;
      return Array.isArray(items);
    };
    if (list("variants", v)) state.thumbs = (v._meta && v._meta["jm/thumbs"]) || {};
    const listed = list("sandboxes", s);
    if (listed) state.sandboxThumbs = (s._meta && s._meta["jm/thumbs"]) || {};
    // A sandbox's files changed since it was compared: what it showed may be no longer so.
    const c = state.compared, compared = c && c.kind === "sandbox" && c.result && !c.result.isError && c.result.structuredContent;
    if (listed && compared && !state.sandboxes.some((x) => x.name === c.name && x.print === compared.print)) state.compared = null;
    if (!state.picked && state.variants && state.variants.length) state.picked = { kind: "variant", name: state.variants[0].name };
    ctx.rerender();
  };

  const run = async (ctx, label, name, args, done, after) => {
    const mine = state;  // another game shown meanwhile: its tab isn't this one's to touch
    state.confirm = "";
    state.compared = null;  // what it showed may be no longer so
    const ok = await ctx.act(label, name, args, done);
    if (mine !== state) return;
    if (ok && after) after();
    await load(ctx);
  };

  // The moment to compare at: a play of the game's (the person's latest unless
  // picked) and a marker of it or its end; with no plays, or "none" picked, a frame.
  // While the plays are read, none; if they can't be, jm's default (play, at: undefined).
  const named = (at) => /^m\d+$/.test(at) ? `marker ${at.slice(1)}` : at === "end" ? "the end of the play" : at === "start" ? "the start" : /^\d+$/.test(at) ? `frame ${at}` : at;
  const momentOf = (ctx) => {
    const answer = ctx.plays();
    if (!answer) return null;
    if (answer.error) return { plays: [], marks: [] };
    const plays = answer.plays;
    const play = plays.find((p) => p.id === state.play) || (state.play === "none" ? null : plays.find((p) => p.by !== "agent"));
    if (!play) return { plays, play: "none", at: String(state.frame), marks: [] };
    const marks = (play.marks || []).map((m) => ({ at: m.split(" ")[0], label: `marker ${m.slice(1)}` }));
    // A moment named otherwise (the agent's "1:05", "start"): kept, an option of its own.
    if (state.at && state.at !== "end" && !marks.some((m) => m.at === state.at)) marks.push({ at: state.at, label: named(state.at) });
    const at = state.at || (marks.length ? marks.at(-1).at : "end");
    return { plays, play: play.id, at, marks };
  };

  // variant_compare's answer as a comparison: its result is the Studio's (games and all), the comparison in it.
  const comparedOf = (r) => !r || r.isError || !r.structuredContent || !r.structuredContent.compared ? (r && r.isError ? r : { isError: true })
    : { structuredContent: r.structuredContent.compared, _meta: r._meta };

  // A comparison the agent made (variant_compare opened the Studio): shown as if
  // compared here, with the form at its moment.
  let adopted = null;  // the call's result shown already: once, not again on coming back to its game
  const adopt = (ctx) => {
    const r = ctx.host.result, s = r && r.structuredContent && r.structuredContent.compared;
    if (!s || r === adopted || s.game !== state.game) return;  // another game's: not this tab's to show
    adopted = r;
    const kind = s.sandbox ? "sandbox" : "variant", frame = ((r._meta && r._meta["jm/frames"]) || [])[0];
    state.picked = { kind, name: s.a };
    if (s.play) Object.assign(state, { play: s.play, at: s.at }); else Object.assign(state, { play: "none", at: null, frame: s.frame });
    state.compared = { name: s.a, kind, b: s.b || "game", play: s.play || "none", at: s.play ? s.at : String(s.frame), result: comparedOf(r) };
    if (frame) (kind === "sandbox" ? state.sandboxThumbs : state.thumbs)[s.a] = frame;
  };

  const compare = async (ctx, name, kind) => {
    const { play, at } = momentOf(ctx) || {}, mine = state, c = state.compared = { name, kind, b: "game", play, at, result: null };
    ctx.rerender();
    const r = await ctx.call("variant_compare", { a: name, ...(kind === "sandbox" ? { sandbox: true } : {}), play, at }).catch((e) => ({ isError: true, content: [{ type: "text", text: String(e && e.message || e) }] }));
    if (mine !== state) return;  // another game shown meanwhile
    const result = comparedOf(r), frame = !result.isError && ((result._meta && result._meta["jm/frames"]) || [])[0];
    if (frame) (kind === "sandbox" ? state.sandboxThumbs : state.thumbs)[name] = frame;  // its picture now, as the next list says
    if (state.compared === c) c.result = result;
    ctx.rerender();
  };

  // A button that asks first (ctx.confirm: in the host's modal, else pressed twice here), then runs name.
  const twice = (ctx, key, label, ask, verb, name, args, done, after) => ctx.confirm({ key, label, ask, tool: name, args,
    armed: state.confirm === key, arm: () => { state.confirm = key; ctx.rerender(); }, go: () => run(ctx, verb, name, args, done, after),
    after: () => { state.compared = null; } });  // what it showed may be no longer so

  const marks = { M: "changed", A: "added", D: "deleted" };

  const changesList = (el, changes) => el("div", { class: "var-changes" }, changes.map((c) => el("div", {},
    el("span", { class: "var-path" }, c.path), " ", el("span", { class: "meta" }, marks[c.status] || c.status),
    (c.entities || []).map((e) => el("div", { class: "meta var-entity" }, `${e.change} ${e.entity}${e.changed ? ": " + e.changed.join(", ") : ""}`)))));

  // Compare's form and what it showed: item (a variant, or a sandbox) beside the
  // game at a frame. The changes are a variant's commits since it split; a
  // sandbox lists its own (comparing adds each scene's per entity).
  const comparing = (ctx, item, kind) => {
    const { el } = ctx.kit, c = state.compared && state.compared.name === item.name && state.compared.kind === kind ? state.compared : null;
    const m = momentOf(ctx) || { plays: [], marks: [], reading: true }, pick = (label, options, value, set) => el("select", { "aria-label": label, class: "var-pick", onchange: (e) => { set(e.target.value); ctx.rerender(); } },
      options.map((o) => el("option", { value: o.value, selected: o.value === value }, o.label)));
    // Standing still, two takes look alike: a play's inputs drive both the same way, to the moment it marked.
    const playPick = m.plays.length ? pick("Play", [...m.plays.map((p) => ({ value: p.id,
      label: `${p.by === "agent" ? "an agent's run" : "your play"} of ${new Date(p.started).toLocaleString([], { dateStyle: "medium", timeStyle: "short" })}` })),
    { value: "none", label: "no input" }], m.play, (v) => { state.play = v; state.at = null; }) : null;
    let atPick = null;
    if (m.play === "none") {
      atPick = el("input", { type: "number", min: "1", "aria-label": "Frame", class: "var-frame", "data-key": "var-frame",
        oninput: (e) => { state.frame = Math.max(1, Math.round(Number(e.target.value)) || 1); } });
      atPick.value = String(state.frame);
    } else if (m.play) atPick = pick("Moment", [...m.marks.map((k) => ({ value: k.at, label: k.label })), { value: "end", label: "its end" }], m.at, (v) => { state.at = v; });
    const where = (x) => x.play === "none" ? `frame ${x.at}` : !x.play ? "your latest play's last marker" : named(x.at);
    let body;
    if (!c) body = el("div", { class: "meta" }, "Compare runs both builds headless, with the play's inputs, and shows them at the same moment.");
    else if (!c.result) body = el("div", { class: "meta" }, `Comparing at ${where(c)}: building and running both…`);
    else if (c.result.isError) body = el("div", { class: "meta" }, said(c.result) || "Couldn't compare.");
    else {
      const s = c.result.structuredContent || {}, frames = (c.result._meta && c.result._meta["jm/frames"]) || [], other = c.b === "game" ? "the game" : c.b;
      body = el("div", { class: "var-compare" },
        el("div", { class: "var-ab" }, [item.name, other].map((label, i) => el("figure", {},
          frames[i] ? el("img", { src: frames[i], alt: `${label} at frame ${s.frame}` }) : el("div", { class: "var-noframe meta" }, (s.errors || [])[i] || "No frame."),
          el("figcaption", {}, `${label} · ${s.play ? `${where(c)} (frame ${s.frame})` : `frame ${s.frame}`}`)))),
        // Seen it there: now play it there.
        // The frame shown, not the moment again: a play still recording has a later end by now.
        kind === "variant" && s.play ? el("div", { class: "head" }, el("button", { disabled: ctx.busy,
          onclick: () => ctx.act("Starting the variant", "variant_play", { name: item.name, play: s.play, at: String(s.frame) }, () => `Variant ${item.name} is open at frame ${s.frame}.`) }, `Play ${item.name} from there`)) : null,
        kind === "sandbox" ? null : (s.changes || []).length ? changesList(el, s.changes) : el("div", { class: "meta" }, `Its commits changed no files since it split from ${other}.`),
        // Each frame runs its files as they are: say when uncommitted work, not the list, is part of the difference.
        kind === "sandbox" ? null : [item.name, other].map((label, i) => (s.uncommitted || [])[i] ? el("div", { class: "meta" }, `${label} has ${s.uncommitted[i]} uncommitted files: its frame shows them, the list doesn't.`) : null));
    }
    const form = el("form", { class: "head", onsubmit: (e) => { e.preventDefault(); compare(ctx, item.name, kind); } },
      el("span", { class: "meta" }, "Against the game"), playPick ? [el("span", { class: "meta" }, "with"), playPick] : null,
      atPick ? [el("span", { class: "meta" }, m.play === "none" ? "at frame" : "at"), atPick] : el("span", { class: "meta" }, m.reading ? "(reading the plays…)" : "at your latest play's last marker"),
      el("button", { class: "primary", type: "submit", disabled: !!(c && !c.result) || !!m.reading }, "Compare"));
    return { form, body, changes: c && c.result && !c.result.isError && (c.result.structuredContent || {}).changes };
  };

  const variantView = (ctx, v) => {
    const { el } = ctx.kit, cmp = comparing(ctx, v, "variant");
    ctx.share(`The person is looking at variant ${v.name} of ${ctx.game.name} (${v.ahead} commits ahead).`, { variant: v.name, dir: v.dir });
    return el("section", { class: "section var-detail" },
      el("div", { class: "head" }, el("h1", {}, v.name), el("span", { class: "meta" }, `${v.branch} · ${v.subject || ""}`), el("span", { class: "spacer" }),
        workHere(ctx, v),
        el("button", { disabled: ctx.busy, onclick: () => ctx.act("Starting the variant", "variant_play", { name: v.name }, () => `Variant ${v.name} is open to play.`) }, "Play"),
        twice(ctx, "merge:" + v.name, "Merge", "Merge into the game?", "Merging", "variant_merge", { name: v.name }, () => `Variant ${v.name} is merged into the game.`),
        twice(ctx, "drop:" + v.name, "Drop", v.changed ? `Drop? (${v.changed} uncommitted files go)` : "Drop?",
          "Dropping", "variant_drop", { name: v.name }, (t) => t, () => { state.picked = null; }),
        el("button", { onclick: () => ctx.host.message([ctx.host.text(`About variant ${v.name} of ${ctx.game.name}: `)], { send: false }) }, "Ask about this")),
      cmp.form,
      cmp.body);
  };

  // A check run in a sandbox (jm build or test there, not in the game): its last
  // outcome by sandbox, { what, running } then { what, ok, text }.
  const check = async (ctx, s, what) => {
    const game = state.game, mine = { what, running: true };
    state.ran[s.name] = mine;
    ctx.rerender();
    let r;
    try { r = await ctx.call("sandbox_run", { name: s.name, args: [what] }); } catch (e) { r = { isError: true, content: [{ type: "text", text: String(e && e.message || e) }] }; }
    if (state.game !== game || state.ran[s.name] !== mine) return;  // another game, or a later run
    const text = ((r && r.content) || []).filter((c) => c.type === "text").map((c) => c.text).join("\n").trim();
    state.ran[s.name] = { what, ok: !(r && r.isError), text };
    ctx.rerender();
  };
  const ranView = (ctx, s) => {
    const { el } = ctx.kit, r = state.ran[s.name];
    if (!r) return null;
    if (r.running) return el("div", { class: "meta" }, `Running jm ${r.what} in the sandbox…`);
    const tail = r.text.split("\n").slice(-12).join("\n");
    return el("div", { class: "var-ran" },
      el("div", { class: "head" }, el("span", { class: r.ok ? "badge" : "badge warn" }, r.ok ? `${r.what} passed` : `${r.what} failed`), el("span", { class: "spacer" }),
        ctx.ask({ title: `${r.what} in sandbox ${s.name}`, text: `jm ${r.what} in the sandbox ${s.name} (${s.dir}) of ${ctx.game.name}:\n${tail}`,
          question: `jm ${r.what} ${r.ok ? "passed" : "failed"} in my sandbox ${s.name}: what does it say?` })),
      tail ? el("pre", { class: "var-out" }, tail) : null);
  };

  // Opens the copy's folder as the game: every tool (and the editor) works there
  // until Back to the game; the Studio says so on every tab.
  const workHere = (ctx, item) => ctx.kit.el("button", { class: "primary", disabled: ctx.busy, title: item.dir, onclick: async () => {
    if (await ctx.act("Opening it", "open_game", { game: item.dir }, () => `Working in ${item.name}: changes go there, not to the game.`)) ctx.go(ctx.link("game"));
  } }, "Work here");

  const sandboxView = (ctx, s) => {
    const { el } = ctx.kit, cmp = comparing(ctx, s, "sandbox");
    ctx.share(`The person is looking at sandbox ${s.name} of ${ctx.game.name}.`, { sandbox: s.name, dir: s.dir });
    const changes = cmp.changes || Object.entries(s.changes || {}).sort().map(([path, status]) => ({ path, status }));
    const keep = state.draft !== null
      ? el("form", { class: "head", onsubmit: (e) => {
          e.preventDefault();
          const as = (state.draft || "").trim();
          if (as) run(ctx, "Keeping it", "sandbox_promote", { name: s.name, variant: as }, () => `Sandbox ${s.name} is variant ${as} now.`,
            () => { state.picked = { kind: "variant", name: as }; state.draft = null; });
        } }, (() => {
          const input = el("input", { placeholder: "The variant's name", "aria-label": "Variant name", "data-key": "var-keep", class: "var-grow",
            oninput: (e) => { state.draft = e.target.value; } });
          input.value = state.draft;
          return input;
        })(), el("button", { class: "primary", type: "submit", disabled: ctx.busy }, "Keep"),
          el("button", { type: "button", onclick: () => { state.draft = null; ctx.rerender(); } }, "Cancel"))
      : null;
    return el("section", { class: "section var-detail" },
      el("div", { class: "head" }, el("h1", {}, s.name), el("span", { class: "badge" }, "sandbox"),
        el("span", { class: "meta" }, `expires ${new Date(s.expires * 1000).toLocaleString([], { dateStyle: "medium", timeStyle: "short" })}`), el("span", { class: "spacer" }),
        workHere(ctx, s),
        el("button", { disabled: !!(state.ran[s.name] && state.ran[s.name].running), onclick: () => check(ctx, s, "build") }, "Build"),
        el("button", { disabled: !!(state.ran[s.name] && state.ran[s.name].running), onclick: () => check(ctx, s, "test") }, "Test"),
        el("button", { disabled: ctx.busy, onclick: () => { state.draft = ""; ctx.rerender(); } }, "Keep as a variant"),
        twice(ctx, "sdrop:" + s.name, "Drop", "Drop?", "Dropping", "sandbox_drop", { name: s.name }, (t) => t, () => { state.picked = null; })),
      keep,
      ranView(ctx, s),
      cmp.form,
      cmp.body,
      changes.length ? changesList(el, changes) : el("div", { class: "meta" }, "The same as the game."));
  };

  const styled = (el) => {
    if (styled.done) return;
    styled.done = true;
    document.head.append(el("style", {}, `
      .var-ran { display: grid; gap: 6px; } .var-ran .head { margin: 0; }
      .var-out { margin: 0; font: 12px/1.45 ui-monospace, Menlo, monospace; background: var(--panel); border: 1px solid var(--line); border-radius: 8px;
        padding: 8px 10px; white-space: pre-wrap; overflow-wrap: anywhere; max-height: 220px; overflow: auto; }
      .var-grow { flex: 1; min-width: 140px; } .var-frame { width: 90px; } .var-pick { max-width: 260px; }
      .var-list { display: grid; gap: 2px; }
      .var-row { all: unset; display: grid; grid-template-columns: 72px minmax(0, 1fr); column-gap: 10px; padding: 5px 6px; border-radius: 6px; cursor: pointer; }
      .var-row:hover, .var-row:focus-visible { background: var(--raised); } .var-row.on { background: var(--accent-soft); }
      .var-row img, .var-row .nothumb { grid-row: span 2; width: 72px; aspect-ratio: 16 / 9; object-fit: cover; border-radius: 4px; background: var(--raised); image-rendering: pixelated; }
      .var-name { font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
      .var-detail { display: grid; gap: 10px; align-content: start; padding: 10px 12px; }
      .var-ab { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
      @media (max-width: 700px) { .var-ab { grid-template-columns: 1fr; } }
      .var-ab figure { margin: 0; display: grid; gap: 4px; } .var-ab img { width: 100%; border-radius: var(--radius); image-rendering: pixelated; background: #000; }
      .var-ab figcaption { color: var(--muted); font-size: 12px; } .var-noframe { aspect-ratio: 16 / 9; display: grid; place-items: center; overflow-wrap: anywhere; background: var(--raised); border-radius: var(--radius); padding: 8px; text-align: center; }
      .var-compare, .var-changes { display: grid; gap: 6px; } .var-path { font: 12px ui-monospace, Menlo, monospace; } .var-entity { padding-left: 14px; }`));
  };

  const row = (ctx, kind, item, thumb, meta) => {
    const { el } = ctx.kit, on = state.picked && state.picked.kind === kind && state.picked.name === item.name;
    return el("button", { class: "var-row" + (on ? " on" : ""), "data-key": `${kind}:${item.name}`, title: item.dir,
      onclick: () => { state.picked = { kind, name: item.name }; state.confirm = ""; state.draft = null; ctx.rerender(); } },
      thumb ? el("img", { src: thumb, alt: "" }) : ctx.kit.nothumb(kind === "sandbox" ? "M6 2h4M6.5 2v4L3 13h10L9.5 6V2M5 10h6" : "M8 2v4M8 6l-4.5 4v4M8 6l4.5 4v4"), el("span", { class: "var-name" }, item.name), el("span", { class: "meta" }, meta));
  };

  const render = (ctx) => {
    const { kit, game } = ctx, { el } = kit;
    styled(el);
    if (!ctx.host.can("callTool")) return kit.empty("This host can't run Journeyman's tools here.");
    if (ctx.copy) return kit.empty(`This is the ${ctx.copy.kind} ${ctx.copy.name}: the game's variants and sandboxes are listed from the game. Back to the game, above, to see them.`);
    if (state.game !== game.path) state = fresh(game.path);
    if (state.asking !== String(ctx.visit)) { state.asking = String(ctx.visit); load(ctx); }
    const route = ctx.params.variant || ctx.params.sandbox ? ctx.params : null;  // /variants?variant=floaty
    if (route && state.routed !== route) { state.routed = route; state.picked = ctx.params.variant ? { kind: "variant", name: ctx.params.variant } : { kind: "sandbox", name: ctx.params.sandbox }; }
    adopt(ctx);
    if (!state.variants && !state.failed.variants) return kit.empty("Reading the variants…");  // both lists come back together
    const variants = state.variants || [];
    const failing = (key) => state.failed[key] ? el("div", { class: "meta" }, `${state.failed[key]}${state[key] ? " (this list may be out of date)" : ""} `,
      el("button", { onclick: () => load(ctx) }, "Try again")) : null;
    const name = el("input", { placeholder: "New variant, e.g. floaty-jump", "aria-label": "New variant's name", "data-key": "var-new", class: "var-grow",
      oninput: (e) => { state.newName = e.target.value; } });
    name.value = state.newName || "";
    const head = el("form", { class: "head", onsubmit: (e) => {
      e.preventDefault();
      const n = (state.newName || "").trim();
      if (n) run(ctx, "Making the variant", "variant_new", { name: n }, () => `Variant ${n} is made from the last commit.`, () => { state.newName = ""; state.picked = { kind: "variant", name: n }; });
    } }, el("h1", {}, "Variants"), name, el("button", { class: "primary", type: "submit", disabled: ctx.busy }, "New variant"));
    const left = el("div", { class: "pane" },
      kit.section(`Variants · ${variants.length}`, failing("variants"), variants.length ? el("div", { class: "var-list" }, variants.map((v) =>
        row(ctx, "variant", v, state.thumbs[v.name], `${v.ahead ? v.ahead + " ahead" : "no new commits"}${v.behind ? ` · ${v.behind} behind` : ""}${v.changed ? ` · ${v.changed} uncommitted` : ""}`)))
        : state.failed.variants ? null : el("div", { class: "meta" }, "None yet: a variant tries an idea beside the game, from its last commit.")),
      state.failed.sandboxes || (state.sandboxes || []).length ? kit.section(`Sandboxes · ${(state.sandboxes || []).length}`, failing("sandboxes"),
        el("div", { class: "var-list" }, (state.sandboxes || []).map((s) => row(ctx, "sandbox", s, state.sandboxThumbs[s.name], `${ctx.kit.plural(Object.keys(s.changes || {}).length, "file differs", "files differ")}`)))) : null);
    const p = state.picked;
    const v = p && p.kind === "variant" && variants.find((x) => x.name === p.name);
    const s = p && p.kind === "sandbox" && (state.sandboxes || []).find((x) => x.name === p.name);
    const right = v ? variantView(ctx, v) : s ? sandboxView(ctx, s)
      : kit.section("Details", el("div", { class: "meta" }, p ? `No ${p.kind} ${p.name}.` : "Pick a variant to compare it with the game, play it, merge or drop it."));
    return el("div", {}, head, kit.split("variants", left, right, { ratio: 0.34 }));
  };

  return { id: "variants", title: "Variants", order: 80, render };
})());
