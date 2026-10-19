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
    root.dataset.bar = root.dataset.mode === "inline" || matchMedia("(max-width: 760px)").matches ? "top" : "side";
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
  // fold and unfold, ↑/↓ move. folded: a Set of ids the caller keeps.
  const tree = ({ items, selected, folded = new Set(), onPick, label = "Tree" }) => {
    const root = el("div", { class: "tree", role: "tree", "aria-label": label });
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
            onclick: () => onPick && onPick(item),
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

  // jm's JSON layout (internal/jsonfmt), without the last newline: small diffs whoever writes a file.
  const jsonText = (v, depth = 0) => {
    const pad = "  ".repeat(depth + 1), end = "  ".repeat(depth);
    if (Array.isArray(v)) {
      if (!v.length) return "[]";
      if (v.every((x) => x === null || typeof x !== "object")) {
        const line = "[" + v.map((x) => jsonText(x)).join(", ") + "]";
        if (line.length <= 72) return line;
      }
      return "[\n" + v.map((x) => pad + jsonText(x, depth + 1)).join(",\n") + "\n" + end + "]";
    }
    if (v && typeof v === "object") {
      const keys = Object.keys(v);
      if (!keys.length) return "{}";
      return "{\n" + keys.map((k) => pad + JSON.stringify(k) + ": " + jsonText(v[k], depth + 1)).join(",\n") + "\n" + end + "}";
    }
    if (typeof v === "number") {
      const n = Math.abs(v) < 1e9 ? Math.round(v * 1e6) / 1e6 : v;
      return Math.abs(n) >= 1e15 ? n.toExponential() : String(n);  // nlohmann's exponent past 1e15
    }
    return JSON.stringify(v);
  };

  // A 16px line icon from an SVG path (in a 16 by 16 box).
  const icon = (d) => el("svg:svg", { viewBox: "0 0 16 16", fill: "none", stroke: "currentColor", "stroke-width": "1.4", "stroke-linejoin": "round",
    "stroke-linecap": "round", "aria-hidden": "true" }, el("svg:path", { d }));
  // A picture's place when there's none: the kind's icon on a plain tile.
  const nothumb = (d) => el("span", { class: "nothumb" }, icon(d));
  const plural = (n, one, many = one + "s") => `${n} ${n === 1 ? one : many}`;

  const empty = (text, ...kids) => el("div", { class: "empty" }, el("div", {}, text), kids);
  const section = (title, ...kids) => el("section", { class: "section" }, el("h2", {}, title), kids);

  return { el, theme, ago, tree, grid, split, empty, section, focusKey, jsonText, icon, nothumb, plural };
})();
