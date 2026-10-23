// Studio's UI kit: elements, host theme, and the pieces every tab is built
// from (tree, thumbnail grid, split panes). Pure DOM: each call makes
// elements; a tab re-renders by making them again. Focusable pieces carry
// data-key, so the shell keeps the focus on the same piece across renders.
const kit = (() => {
  "use strict";
  const svgNS = "http://www.w3.org/2000/svg";
  // el("button", { class, style: {...}, onclick, ...attributes }, ...kids); "svg:rect" for SVG.
  const el = (tag, attrs = {}, ...kids) => {
    const e = tag.startsWith("svg:") ? document.createElementNS(svgNS, tag.slice(4)) : document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === "class") e.setAttribute("class", v);
      else if (k === "style" && typeof v === "object") Object.assign(e.style, v);
      else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
      else e.setAttribute(k, v === true ? "" : v);
    }
    for (const kid of kids.flat(Infinity)) if (kid !== null && kid !== undefined && kid !== false) e.append(kid.nodeType ? kid : String(kid));
    return e;
  };

  // The host's look on the page: theme, its style variables and fonts, safe
  // areas (--safe-*), the display mode (data-mode), and where the tabs go
  // (data-bar: a side bar, or a row on top when inline or narrow).
  let fonts = "";
  const theme = (host) => {
    const root = document.documentElement, c = host.context || {};
    root.dataset.theme = host.theme || (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
    root.dataset.mode = host.displayMode || "inline";
    // Picture-in-picture: no tabs, the page only (its own way back).
    root.dataset.bar = root.dataset.mode === "pip" ? "none" : root.dataset.mode === "inline" || matchMedia("(max-width: 760px)").matches ? "top" : "side";
    const vars = (c.styles && c.styles.variables) || {};
    for (const [k, v] of Object.entries(vars)) if (k.startsWith("--") && typeof v === "string") root.style.setProperty(k, v);
    const css = c.styles && c.styles.css && c.styles.css.fonts;
    if (css && css !== fonts) { fonts = css; document.head.append(el("style", {}, css)); }
    const inset = c.safeAreaInsets || {};
    for (const side of ["top", "right", "bottom", "left"]) root.style.setProperty("--safe-" + side, (Number(inset[side]) || 0) + "px");
  };

  const ago = (unix) => {
    if (!unix) return "never";
    const s = Date.now() / 1000 - unix;
    if (s < 90) return "just now";
    if (s < 3600) return `${Math.round(s / 60)} min ago`;
    if (s < 86400) return `${Math.round(s / 3600)} h ago`;
    if (s < 86400 * 14) return `${Math.round(s / 86400)} days ago`;
    return new Date(unix * 1000).toLocaleDateString([], { dateStyle: "medium" });
  };

  // Arrow keys move the focus along items (all in order); Home/End to the ends.
  const roving = (items, vertical) => (e) => {
    const keys = vertical ? ["ArrowUp", "ArrowDown"] : ["ArrowLeft", "ArrowRight"];
    const at = items.indexOf(e.target);
    let to = { [keys[0]]: at - 1, [keys[1]]: at + 1, Home: 0, End: items.length - 1 }[e.key];
    if (to === undefined || at < 0) return;
    e.preventDefault();
    to = Math.max(0, Math.min(items.length - 1, to));
    items[to].focus();
  };

  // A tree of { id, label, meta?, children? }: click or Enter picks, ←/→
  // fold and unfold, ↑/↓ move. folded: a Set of ids the caller keeps. A row
  // with children onPick returns false for (a folder, not a thing to pick)
  // folds or unfolds instead.
  // wrap: labels are sentences, shown whole (a note), not cut to a line.
  const tree = ({ items, selected, folded = new Set(), onPick, label = "Tree", wrap = false }) => {
    const root = el("div", { class: wrap ? "tree wrap" : "tree", role: "tree", "aria-label": label });
    const rows = [];
    const draw = () => {
      rows.length = 0;
      root.replaceChildren();
      const walk = (list, depth) => {
        for (const item of list) {
          const kids = item.children && item.children.length ? item.children : null;
          const open = kids && !folded.has(item.id);
          const fold = (on) => { if (kids && on !== folded.has(item.id)) { if (on) folded.add(item.id); else folded.delete(item.id); draw(); focusKey("tree:" + item.id); } };
          const row = el("button", { class: "row" + (item.id === selected ? " on" : ""), role: "treeitem", "data-key": "tree:" + item.id,
            "aria-selected": item.id === selected ? "true" : "false", "aria-expanded": kids ? String(!!open) : undefined,
            style: { paddingLeft: 6 + depth * 14 + "px" }, title: item.title || item.label,
            onclick: () => { if ((!onPick || onPick(item) === false) && kids) fold(!!open); },
            onkeydown: (e) => {
              if (e.key === "ArrowLeft" && open) { e.preventDefault(); fold(true); }
              else if (e.key === "ArrowRight" && kids && !open) { e.preventDefault(); fold(false); }
            } },
            el("span", { class: "twisty" + (open ? " open" : ""), onclick: (e) => { e.stopPropagation(); fold(!!open); } }, kids ? icon("M6 4l4 4-4 4") : ""),
            el("span", { class: "label" }, item.label), item.meta ? el("span", { class: "meta" }, item.meta) : null);
          rows.push(row);
          root.append(row);
          if (open) walk(kids, depth + 1);
        }
      };
      walk(items, 0);
      for (const row of rows) row.addEventListener("keydown", roving(rows, true));
    };
    draw();
    return root;
  };
  const focusKey = (key) => {
    const at = key && document.querySelector(`[data-key="${CSS.escape(key)}"]`);
    if (at) at.focus();
  };

  // Thumbnail cards of { id, title, meta, thumb?, badges? }: click picks,
  // double-click or Enter opens; actions(item) are buttons on the picked one.
  const grid = ({ items, selected, onPick, onOpen, actions, label = "Items" }) => {
    const cards = [];
    const root = el("div", { class: "grid", role: "listbox", "aria-label": label }, items.map((item) => {
      const on = item.id === selected;
      const pick = el("button", { class: "pick", role: "option", "aria-selected": on ? "true" : "false", "data-key": "card:" + item.id, title: item.id,
        onclick: () => onPick && onPick(item), ondblclick: () => onOpen && onOpen(item),
        onkeydown: (e) => { if (e.key === "Enter" && onOpen) { e.preventDefault(); onOpen(item); } } },
        el("div", { class: "thumb" }, item.thumb ? el("img", { src: item.thumb, alt: "" })
          : el("span", { class: "letter" }, (item.title || "?").slice(0, 1).toUpperCase())),
        el("div", { class: "card-body" },
          el("div", { class: "name" }, el("span", {}, item.title), (item.badges || []).map((b) => el("span", { class: "badge" + (b.warn ? " warn" : "") }, b.text))),
          el("div", { class: "meta" }, item.meta)));
      cards.push(pick);
      const acts = on && actions ? actions(item) : null;
      return el("div", { class: "card" + (on ? " on" : "") }, pick, acts && acts.length ? el("div", { class: "actions" }, acts) : null);
    }));
    root.addEventListener("keydown", roving(cards, false));
    return root;
  };

  // Two panes side by side (stacked on narrow pages), the line between them
  // dragged or arrowed to resize; each key's split is kept for the page's life.
  const splits = {};
  const split = (key, a, b, { ratio = 0.62 } = {}) => {
    const root = el("div", { class: "split" });
    const apply = () => root.style.setProperty("--split", (splits[key] = Math.max(0.15, Math.min(0.85, splits[key] ?? ratio))) * 100 + "%");
    const bar = el("div", { class: "split-bar", role: "separator", tabindex: "0", "aria-orientation": "vertical", "data-key": "split:" + key,
      onkeydown: (e) => {
        const d = { ArrowLeft: -0.04, ArrowRight: 0.04 }[e.key];
        if (d) { e.preventDefault(); splits[key] = (splits[key] ?? ratio) + d; apply(); }
      },
      onpointerdown: (e) => {
        bar.setPointerCapture(e.pointerId);
        const move = (m) => { const r = root.getBoundingClientRect(); splits[key] = (m.clientX - r.left) / r.width; apply(); };
        bar.addEventListener("pointermove", move);
        bar.addEventListener("pointerup", () => bar.removeEventListener("pointermove", move), { once: true });
      } });
    apply();
    root.append(el("div", { class: "pane" }, a), bar, el("div", { class: "pane" }, b));
    return root;
  };

  // edits are what makes prev next, by path: a value changed, a key added or
  // taken away; a list of another length whole.
  const own = (o, k) => Object.prototype.hasOwnProperty.call(o, k);  // "constructor" is a key like any
  const edits = (prev, next, path = []) => {
    const isObject = (v) => v !== null && typeof v === "object" && !Array.isArray(v);
    if (isObject(prev) && isObject(next)) {
      return [...Object.keys(prev).filter((k) => !own(next, k)).map((k) => ({ path: [...path, k], remove: true })),
        ...Object.keys(next).flatMap((k) => own(prev, k) ? edits(prev[k], next[k], [...path, k]) : [{ path: [...path, k], value: next[k] }])];
    }
    if (Array.isArray(prev) && Array.isArray(next) && prev.length === next.length) return next.flatMap((v, i) => edits(prev[i], v, [...path, i]));
    return JSON.stringify(prev) === JSON.stringify(next) ? [] : [{ path, value: next }];
  };

  // The JSON file the host opened, edited on this page: read through the host
  // for its version, writable only when it reads back as jm fmt writes it; a
  // save is jm's text for the edits (json_edit: the layout is jm's alone).
  // One save at a time; a read or save that ends after another began changes
  // nothing. say(notice) shows what happened; changed() re-renders.
  const jsonFile = (host, { say, changed }) => {
    let reads = 0, wanted = "";
    const f = { file: null, saving: false };  // file: { uri, text, doc, etag, writable, why, unchecked }
    // laid is text with edits made, as jm fmt writes it.
    const laid = async (text, made) => {
      const r = await host.callTool("json_edit", { text, edits: made });
      if (!r || r.isError || !r.structuredContent) throw new Error(said(r) || "jm didn't lay it out");
      return r.structuredContent.text;
    };
    // read: false when another read began meanwhile (this one then changes nothing).
    f.read = async (uri) => {
      const n = ++reads;
      wanted = uri;
      let file = null, notice = "";
      try {
        const r = await host.readResource(uri);
        file = { uri, text: r.text, doc: JSON.parse(r.text), etag: r.etag, writable: false, why: "the host won't save it" };
        if (r.writable && r.etag) {
          file.why = "it isn't as jm fmt writes it (jm fmt it)";
          try { file.writable = (await laid(r.text, [])) === r.text; }
          catch { file.writable = file.unchecked = true; }  // jm couldn't say now: the save asks again
        }
      } catch (e) { notice = "Couldn't read the file: " + (e && e.message || e); }
      if (n !== reads) return false;
      if (file) f.file = file; else say(notice);
      changed();
      return true;
    };
    // open reads uri once (again only after another file).
    f.open = (uri) => { if (uri && uri !== wanted) f.read(uri); };
    f.editable = (uri) => !!(f.file && f.file.writable && f.file.uri === uri);
    // save writes edit(copy of the doc); a conflict reads the file again. after(outcome) is the page's
    // follow-up (lay it out again), awaited while still saving. "saved" | "conflict" | "failed", or
    // null: nothing written (a save under way), or another file was opened since it began.
    f.save = async (edit, after) => {
      const s = f.file, n = reads;
      if (!s || f.saving) return null;
      const doc = JSON.parse(JSON.stringify(s.doc));
      edit(doc);
      f.saving = true; say("Saving…"); changed();
      let outcome = "failed", notice = "Couldn't save.";
      try {
        if (s.unchecked && (await laid(s.text, [])) !== s.text) { s.writable = false; throw new Error(s.why); }
        s.unchecked = false;
        const text = await laid(s.text, edits(s.doc, doc));
        const r = await host.writeResource(s.uri, text, s.etag);
        if (r && r.outcome === "saved") { Object.assign(s, { text, doc, etag: r.etag }); outcome = "saved"; notice = "Saved."; }
        else if (r && r.outcome === "conflict") { outcome = "conflict"; notice = "The file changed since: read it again, make the change again."; }
        else if (r && r.outcome === "too-large") notice = `Too large to save (at most ${r.maxBytes} bytes).`;
      } catch (e) { notice = "Couldn't save: " + (e && e.message || e); }
      const stale = () => { f.saving = false; changed(); return null; };
      if (n !== reads) return stale();
      say(notice);
      if (outcome === "conflict" && !(await f.read(s.uri))) return stale();
      try { if (after) await after(outcome); } catch (e) { say(notice + " Couldn't show it again: " + (e && e.message || e)); }
      f.saving = false;
      changed();
      return outcome;
    };
    return f;
  };

  // said is what a tool's answer says (its text parts, joined).
  const said = (r) => ((r && r.content) || []).filter((c) => c.type === "text").map((c) => c.text).join(" ");

  // answer is what call() came to: its answer, or a throw (now or later) as an isError one.
  const answer = (call) => Promise.resolve().then(call).catch((e) => ({ isError: true, content: [{ type: "text", text: String(e && e.message || e) }] }));

  // latest(owner) is a channel of calls of which only the newest counts:
  // ask(call) is what call() came to (answer), or null once another ask began
  // or owner() is no longer what it was (another game shown).
  const latest = (owner) => {
    let asked = 0;
    return async (call) => {
      const n = ++asked, mine = owner();
      const r = await answer(call);
      return n === asked && owner() === mine ? r : null;
    };
  };

  // A 16px line icon from an SVG path (in a 16 by 16 box).
  const icon = (d) => el("svg:svg", { viewBox: "0 0 16 16", fill: "none", stroke: "currentColor", "stroke-width": "1.4", "stroke-linejoin": "round",
    "stroke-linecap": "round", "aria-hidden": "true" }, el("svg:path", { d }));
  // A picture's place when there's none: the kind's icon on a plain tile.
  const nothumb = (d) => el("span", { class: "nothumb" }, icon(d));
  const plural = (n, one, many = one + "s") => `${n} ${n === 1 ? one : many}`;
  // A play's length, "1:05"; when it started, in the person's words.
  const duration = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`;
  const started = (iso) => { const d = new Date(iso); return isNaN(d) ? iso : d.toLocaleString([], { dateStyle: "medium", timeStyle: "short" }); };

  const empty = (text, ...kids) => el("div", { class: "empty" }, el("div", {}, text), kids);
  const section = (title, ...kids) => el("section", { class: "section" }, el("h2", {}, title), kids);

  return { el, theme, ago, tree, grid, split, empty, section, focusKey, edits, jsonFile, said, answer, latest, icon, nothumb, plural, duration, started };
})();
