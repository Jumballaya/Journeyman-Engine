// Journeyman site behavior. Every feature is optional: each one looks for its markup and does nothing without it.
(() => {
  const root = document.documentElement.dataset.root || "./";
  const reduceMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const isMac = /mac/i.test(navigator.userAgentData?.platform || navigator.platform);
  const store = {
    get(k) { try { return localStorage.getItem(k); } catch { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); } catch {} },
  };
  document.documentElement.classList.add("js");

  // Screen-reader announcements for things that change without moving focus.
  const announcer = document.querySelector("[data-announce]");
  function announce(text) {
    if (!announcer) return;
    announcer.textContent = "";
    setTimeout(() => (announcer.textContent = text), 30);
  }

  // ---- theme: system -> light -> dark ----
  const themeBtn = document.querySelector("[data-theme-toggle]");
  const themeIcons = { system: "ph-circle-half", light: "ph-sun", dark: "ph-moon" };
  function applyTheme(mode) {
    if (mode === "system") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = mode;
    const bg = getComputedStyle(document.body).backgroundColor;
    document.querySelectorAll('meta[name="theme-color"]').forEach((m) => {
      m.content = mode === "system" ? (m.media.includes("dark") ? "#18191c" : "#f4f5f7") : bg;
    });
    if (themeBtn) {
      themeBtn.querySelector("i").className = "ph " + themeIcons[mode];
      themeBtn.setAttribute("aria-label", `Theme: ${mode}. Change theme`);
      themeBtn.title = `Theme: ${mode}`;
    }
  }
  let theme = store.get("jm-theme") || "system";
  applyTheme(theme);
  themeBtn?.addEventListener("click", () => {
    theme = { system: "light", light: "dark", dark: "system" }[theme];
    store.set("jm-theme", theme);
    applyTheme(theme);
    announce(`Theme: ${theme}`);
  });

  // ---- mobile menu ----
  const menuBtn = document.querySelector("[data-menu-toggle]");
  const menu = document.querySelector(".mobile-menu");
  menuBtn?.addEventListener("click", () => {
    const open = menu.dataset.open !== "true";
    menu.dataset.open = String(open);
    menuBtn.setAttribute("aria-expanded", String(open));
    menuBtn.querySelector("i").className = "ph " + (open ? "ph-x" : "ph-list");
  });

  // ---- copying: code blocks and prompts share one routine ----
  // On success the button says "Copied". If the clipboard is unavailable (file://, permissions),
  // the text is selected instead so the visitor can press the copy shortcut themselves.
  async function copyText(text, sourceEl, btn, idleLabel) {
    const label = btn.querySelector("span");
    const glyph = btn.querySelector("i");
    let message;
    try {
      await navigator.clipboard.writeText(text);
      message = "Copied";
      glyph.className = "ph ph-check";
    } catch {
      const range = document.createRange();
      range.selectNodeContents(sourceEl);
      const sel = getSelection();
      sel.removeAllRanges();
      sel.addRange(range);
      message = `Selected. Press ${isMac ? "⌘C" : "Ctrl C"}`;
    }
    btn.classList.add("done");
    label.textContent = message;
    announce(message);
    setTimeout(() => {
      btn.classList.remove("done");
      label.textContent = idleLabel;
      glyph.className = "ph ph-copy";
    }, 1800);
  }

  document.querySelectorAll("pre").forEach((pre) => {
    if (pre.closest(".no-copy")) return;
    let host = pre.parentElement;
    if (!host.classList.contains("code")) {
      host = document.createElement("div");
      host.className = "code";
      pre.replaceWith(host);
      host.appendChild(pre);
    }
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "copy";
    btn.innerHTML = '<i class="ph ph-copy" aria-hidden="true"></i><span>Copy</span>';
    btn.addEventListener("click", () => copyText(pre.dataset.copy ?? pre.innerText.replace(/\n$/, ""), pre, btn, "Copy"));
    host.appendChild(btn);
  });

  document.querySelectorAll("[data-copy-prompt]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const source = btn.closest(".prompt").querySelector("pre");
      copyText(source.innerText.trim(), source, btn, "Copy prompt");
    });
  });

  // ---- platform detection, shared by tabs and the download page ----
  function detectOS() {
    const ua = navigator.userAgent.toLowerCase();
    const p = (navigator.userAgentData?.platform || navigator.platform || ua).toLowerCase();
    if (p.includes("win")) return "windows";
    if ((p.includes("linux") || p.includes("x11")) && !ua.includes("android")) return "linux";
    return "macos";
  }
  const detected = detectOS();
  const osName = { macos: "macOS", linux: "Linux", windows: "Windows" };

  // ---- tabs (role=tablist). Groups with data-sync share a remembered choice. ----
  document.querySelectorAll(".tabs").forEach((group) => {
    const tabs = [...group.querySelectorAll('[role="tab"]')];
    const sync = group.dataset.sync;
    function select(tab, remember) {
      tabs.forEach((t) => {
        const on = t === tab;
        t.setAttribute("aria-selected", String(on));
        t.tabIndex = on ? 0 : -1;
        document.getElementById(t.getAttribute("aria-controls")).hidden = !on;
      });
      if (sync && remember) {
        store.set("jm-tab-" + sync, tab.dataset.value);
        document.querySelectorAll(`.tabs[data-sync="${sync}"]`).forEach((other) => {
          if (other === group) return;
          const match = other.querySelector(`[data-value="${tab.dataset.value}"]`);
          if (match?.getAttribute("aria-selected") !== "true") match?.click();
        });
      }
    }
    tabs.forEach((t, i) => {
      t.addEventListener("click", () => select(t, true));
      t.addEventListener("keydown", (e) => {
        const to = { ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: tabs.length - 1 }[e.key];
        if (to === undefined) return;
        e.preventDefault();
        const next = tabs[(to + tabs.length) % tabs.length];
        next.focus();
        select(next, true);
      });
    });
    const wanted = (sync && store.get("jm-tab-" + sync)) || (sync === "os" ? detected : null);
    select(tabs.find((t) => t.dataset.value === wanted) || tabs[0], false);
    const note = group.querySelector(".detected");
    if (note && sync === "os") note.textContent = `Your browser says ${osName[detected]}. Pick another tab if that's wrong.`;
  });

  // ---- reveal on scroll ----
  const reveals = document.querySelectorAll(".reveal");
  if ("IntersectionObserver" in window && !reduceMotion) {
    const io = new IntersectionObserver((entries) => {
      for (const e of entries) if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); }
    }, { rootMargin: "0px 0px -8% 0px" });
    reveals.forEach((el) => io.observe(el));
  } else reveals.forEach((el) => el.classList.add("in"));

  // ---- table of contents scrollspy ----
  const tocLinks = [...document.querySelectorAll(".toc a[href^='#']")];
  if (tocLinks.length && "IntersectionObserver" in window) {
    const byId = new Map(tocLinks.map((a) => [decodeURIComponent(a.hash.slice(1)), a]));
    const heads = [...byId.keys()].map((id) => document.getElementById(id)).filter(Boolean);
    const visible = new Set();
    const spy = new IntersectionObserver((entries) => {
      for (const e of entries) e.isIntersecting ? visible.add(e.target.id) : visible.delete(e.target.id);
      const current = heads.find((h) => visible.has(h.id)) ||
        heads.filter((h) => h.getBoundingClientRect().top < 120).pop() || heads[0];
      tocLinks.forEach((a) => {
        const on = byId.get(current?.id) === a;
        a.classList.toggle("active", on);
        if (on) a.setAttribute("aria-current", "location"); else a.removeAttribute("aria-current");
      });
    }, { rootMargin: "-70px 0px -65% 0px" });
    heads.forEach((h) => spy.observe(h));
  }

  // ---- search palette (combobox + listbox) ----
  const dialog = document.getElementById("search");
  const input = dialog?.querySelector("input");
  const list = dialog?.querySelector('[role="listbox"]');
  const status = dialog?.querySelector("[data-search-status]");
  let results = [];
  let cursor = 0;
  const escHTML = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
  // Every term must appear somewhere. Title matches outrank page names, which outrank body text.
  function score(item, terms) {
    const title = item.t.toLowerCase();
    const where = item.p.toLowerCase();
    const body = (item.x || "").toLowerCase();
    let s = 0;
    for (const term of terms) {
      if (title.startsWith(term)) s += 8;
      else if (title.includes(term)) s += 6;
      else if (where.includes(term)) s += 2;
      else if (body.includes(term)) s += 1 + Math.min(4, body.split(term).length - 2) * 0.25;
      else return 0;
    }
    return s + (item.k === "page" ? 0.5 : 0);
  }
  // A short excerpt of the body around the first matching term, with the term marked.
  function snippet(item, terms) {
    const body = item.x || "";
    const lower = body.toLowerCase();
    const term = terms.find((t) => lower.includes(t) && !item.t.toLowerCase().includes(t));
    if (!term) return "";
    const at = lower.indexOf(term);
    const start = Math.max(0, at - 40);
    const text = (start ? "…" : "") + body.slice(start, at + term.length + 60) + "…";
    return escHTML(text).replace(new RegExp(term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "gi"), (m) => `<mark>${m}</mark>`);
  }
  function setActive(i) {
    list.querySelector(`#sr-${cursor}`)?.setAttribute("aria-selected", "false");
    cursor = i;
    const el = list.querySelector(`#sr-${cursor}`);
    if (!el) { input.removeAttribute("aria-activedescendant"); return; }
    el.setAttribute("aria-selected", "true");
    el.scrollIntoView({ block: "nearest" });
    input.setAttribute("aria-activedescendant", el.id);
  }
  function render() {
    const terms = input.value.trim().toLowerCase().split(/\s+/).filter(Boolean);
    const index = window.JM_SEARCH || [];
    if (!terms.length) results = index.filter((i) => i.k === "page");
    else results = index.map((i) => [score(i, terms), i]).filter(([s]) => s > 0)
      .sort((a, b) => b[0] - a[0]).slice(0, 30).map(([, i]) => i);
    cursor = 0;
    if (!results.length) {
      list.innerHTML = `<li role="none" class="empty">Nothing matches “${escHTML(input.value)}”. Try fewer words, or an API name like spawn or Camera.</li>`;
      input.setAttribute("aria-expanded", "false");
      input.removeAttribute("aria-activedescendant");
      status.textContent = "No results";
      return;
    }
    list.innerHTML = results.map((r, i) => {
      const detail = r.k === "page" ? escHTML(r.p.slice(0, 110)) : [escHTML(r.p), snippet(r, terms)].filter(Boolean).join(" · ");
      return `<li role="none"><a href="${root}${r.u}" id="sr-${i}" role="option" aria-selected="${i === 0}" tabindex="-1"><b>${escHTML(r.t)}</b><small>${detail}</small></a></li>`;
    }).join("");
    input.setAttribute("aria-expanded", "true");
    input.setAttribute("aria-activedescendant", "sr-0");
    status.textContent = terms.length ? `${results.length} result${results.length === 1 ? "" : "s"}` : "";
  }
  function openSearch() {
    if (!dialog || dialog.open) return;
    dialog.showModal();
    input.value = "";
    render();
    input.focus();
  }
  document.querySelectorAll("[data-search-open]").forEach((b) => b.addEventListener("click", openSearch));
  input?.addEventListener("input", render);
  input?.addEventListener("keydown", (e) => {
    if (!results.length) return;
    if (e.key === "ArrowDown") { e.preventDefault(); setActive((cursor + 1) % results.length); }
    else if (e.key === "ArrowUp") { e.preventDefault(); setActive((cursor - 1 + results.length) % results.length); }
    else if (e.key === "Enter") { location.href = root + results[cursor].u; dialog.close(); }
  });
  list?.addEventListener("mousemove", (e) => {
    const a = e.target.closest('[role="option"]');
    if (a) { const i = +a.id.slice(3); if (i !== cursor) setActive(i); }
  });
  dialog?.addEventListener("click", (e) => { if (e.target === dialog) dialog.close(); });
  document.addEventListener("keydown", (e) => {
    const typing = /^(input|textarea|select)$/i.test(document.activeElement?.tagName || "");
    if ((e.key === "k" && (e.metaKey || e.ctrlKey)) || (e.key === "/" && !typing)) {
      e.preventDefault();
      openSearch();
    }
  });
  document.querySelectorAll("[data-mod-k]").forEach((k) => (k.textContent = isMac ? "⌘K" : "Ctrl K"));

  // ---- lightbox: any element with data-lightbox="group" and data-src ----
  const lb = document.getElementById("lightbox");
  if (lb) {
    const img = lb.querySelector("img");
    const cap = lb.querySelector(".lb-cap");
    let group = [];
    let at = 0;
    function show(i) {
      at = (i + group.length) % group.length;
      const light = document.documentElement.dataset.theme === "light" ||
        (!document.documentElement.dataset.theme && matchMedia("(prefers-color-scheme: light)").matches);
      img.src = (light && group[at].dataset.srcLight) || group[at].dataset.src;
      img.alt = group[at].dataset.alt || "";
      cap.textContent = `${group[at].dataset.alt || ""} · ${at + 1} of ${group.length}`;
    }
    document.querySelectorAll("[data-lightbox]").forEach((el) => {
      el.addEventListener("click", () => {
        group = [...document.querySelectorAll(`[data-lightbox="${el.dataset.lightbox}"]`)];
        show(group.indexOf(el));
        lb.showModal();
      });
    });
    lb.querySelector(".lb-prev").addEventListener("click", () => show(at - 1));
    lb.querySelector(".lb-next").addEventListener("click", () => show(at + 1));
    lb.querySelector(".lb-close").addEventListener("click", () => lb.close());
    lb.addEventListener("click", (e) => { if (e.target === lb) lb.close(); });
    lb.addEventListener("keydown", (e) => {
      if (e.key === "ArrowRight") show(at + 1);
      if (e.key === "ArrowLeft") show(at - 1);
    });
  }

  // ---- frame scrubber ----
  const scrub = document.querySelector("[data-scrub]");
  if (scrub) {
    const frames = JSON.parse(scrub.dataset.frames);
    const base = scrub.dataset.base;
    const range = scrub.querySelector("input[type=range]");
    const screen = scrub.querySelector(".scrub-screen img");
    const readout = scrub.querySelector("[data-frame]");
    const file = scrub.querySelector("[data-file]");
    const playBtn = scrub.querySelector("[data-play]");
    const src = (i) => `${base}f${String(i).padStart(2, "0")}.jpg`;
    // Fetch the other frames only once the visitor is about to use them.
    let preloaded = false;
    function preload() {
      if (preloaded) return;
      preloaded = true;
      frames.forEach((_, i) => { const im = new Image(); im.src = src(i); });
    }
    ["pointerenter", "focusin", "touchstart"].forEach((ev) => scrub.addEventListener(ev, preload, { once: true, passive: true }));
    function set(i) {
      i = Math.max(0, Math.min(frames.length - 1, i));
      range.value = i;
      screen.src = src(i);
      readout.textContent = frames[i];
      file.textContent = `frame_${String(frames[i]).padStart(5, "0")}.png`;
      screen.alt = `Strike Wing at frame ${frames[i]}`;
      range.setAttribute("aria-valuetext", `Frame ${frames[i]} of ${frames.at(-1)}`);
    }
    let timer = null;
    function stop() {
      clearInterval(timer);
      timer = null;
      playBtn.querySelector("i").className = "ph ph-play";
      playBtn.setAttribute("aria-pressed", "false");
    }
    range.addEventListener("input", () => { stop(); set(+range.value); });
    playBtn.addEventListener("click", () => {
      if (timer) return stop();
      preload();
      if (+range.value >= frames.length - 1) set(0);
      playBtn.querySelector("i").className = "ph ph-pause";
      playBtn.setAttribute("aria-pressed", "true");
      timer = setInterval(() => {
        if (+range.value >= frames.length - 1) return stop();
        set(+range.value + 1);
      }, reduceMotion ? 700 : 260);
    });
    set(0);
  }

  // ---- download page: what, then platform, then the button. ?kind=editor&os=linux preselects. ----
  const dl = document.querySelector("[data-download]");
  if (dl) {
    const files = {
      cli: { macos: "journeyman-cli-darwin-arm64.tar.gz", "macos-intel": "journeyman-cli-darwin-amd64.tar.gz", linux: "journeyman-cli-linux-amd64.tar.gz", windows: "journeyman-cli-windows-amd64.zip" },
      editor: { macos: "journeyman-editor-darwin-arm64.zip", "macos-intel": "journeyman-editor-darwin-amd64.zip", linux: "journeyman-editor-linux-amd64.tar.gz", windows: "journeyman-editor-windows-amd64.zip" },
    };
    const names = { macos: "macOS, Apple silicon", "macos-intel": "macOS, Intel", linux: "Linux x64", windows: "Windows x64" };
    const latest = "https://github.com/Jumballaya/Journeyman-Engine/releases/latest/download/";
    const params = new URLSearchParams(location.search);
    let kind = files[params.get("kind")] ? params.get("kind") : store.get("jm-dl-kind") || "cli";
    let os = names[params.get("os")] ? params.get("os") : store.get("jm-dl-os") || detected;
    const btn = dl.querySelector("[data-dl-btn]");
    const label = dl.querySelector("[data-dl-label]");
    const fname = dl.querySelector("[data-dl-file]");
    const note = dl.querySelector("[data-dl-detected]");
    function update(fromUser) {
      const f = files[kind][os];
      btn.href = latest + f;
      label.textContent = `Download for ${names[os]}`;
      fname.textContent = f;
      dl.querySelectorAll("input[name=kind]").forEach((r) => (r.checked = r.value === kind));
      dl.querySelectorAll("[data-os]").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.os === os)));
      document.querySelectorAll(".files-table tr[data-file]").forEach((tr) => tr.classList.toggle("hit", tr.dataset.file === f));
      if (note) note.textContent = detected === "macos" && os.startsWith("macos")
        ? "Browsers don't say which Mac chip you have. Pick Intel if your Mac is from before 2021."
        : `Your browser says ${osName[detected]}.`;
      if (fromUser) {
        history.replaceState(null, "", `?kind=${kind}&os=${os}`);
        announce(`${label.textContent}: ${f}`);
      }
    }
    dl.querySelectorAll("input[name=kind]").forEach((r) => r.addEventListener("change", () => { kind = r.value; store.set("jm-dl-kind", kind); update(true); }));
    dl.querySelectorAll("[data-os]").forEach((b) => b.addEventListener("click", () => { os = b.dataset.os; store.set("jm-dl-os", os); update(true); }));
    update(false);
  }
})();
