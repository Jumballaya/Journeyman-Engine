// Builds the Journeyman site into dist/. Usage: node build.mjs [path/to/Journeyman-Engine]
// The docs pages are rendered from the repo's docs/*.md (the repo is this folder's parent by default).
// Every page is a directory with an index.html and links are relative, so dist/ works on GitHub Pages
// under any base path and from a plain static server.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createHash } from "node:crypto";
import { marked } from "marked";
import { agentInstallGuide, agentPrompt } from "./agent-install.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(process.argv[2] || path.join(here, ".."));
const out = path.join(here, "dist");
const GH = "https://github.com/Jumballaya/Journeyman-Engine";

// Downloads link to the newest published release, by its tag, so a page always matches the files
// it names. Resolved at build time; the Pages workflow rebuilds when a release is published. Offline or before any release, links
// fall back to the releases page.
const RELEASE = await (async () => {
  try {
    const headers = { Accept: "application/vnd.github+json", "User-Agent": "journeyman-site" };
    if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
    const res = await fetch("https://api.github.com/repos/Jumballaya/Journeyman-Engine/releases?per_page=20", { headers });
    if (!res.ok) throw new Error(`GitHub API ${res.status}`);
    const rel = (await res.json()).find((r) => !r.draft);
    return rel ? { tag: rel.tag_name, assets: new Set(rel.assets.map((x) => x.name)) } : null;
  } catch (e) {
    console.warn(`release lookup failed (${e.message}); download links go to the releases page`);
    return null;
  }
})();
const RELEASES = `${GH}/releases`;
// Where the site is published; the agent prompt and guide need absolute URLs.
const SITE = process.env.SITE_URL || "https://jumballaya.github.io/Journeyman-Engine/";
const AGENT_PROMPT = agentPrompt(SITE);
// The Mastra-style button: copies a prompt that sends an agent to the install guide. The prompt
// itself stays out of the page; only the button shows.
const agentButton = (cls = "btn btn-ghost") =>
  `<button class="${cls} agent-copy" type="button" data-copy-text="${esc(AGENT_PROMPT)}" data-done="Copied. Paste it into your agent">${icon("robot")}<span>Copy prompt for your agent</span></button>`;
const hasAsset = (f) => !!RELEASE?.assets.has(f);
// The URL of a release file, or the releases page when the newest release doesn't have it.
const asset = (f) => (hasAsset(f) ? `${GH}/releases/download/${RELEASE.tag}/${f}` : RELEASES);

// A short content hash per asset, appended as ?v=, so a deploy never serves stale CSS or JS
// from a browser's cache (GitHub Pages caches for ten minutes).
const version = (rel) => createHash("sha256").update(fs.readFileSync(path.join(here, "src", rel))).digest("hex").slice(0, 10);
const ASSET_V = { css: version("css/site.css"), js: version("js/site.js"), search: Date.now().toString(36) };  // the index is generated, so it gets the build time
const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
const unesc = (s) => String(s).replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, "&");
const icon = (name) => `<i class="ph ph-${name}" aria-hidden="true"></i>`;
const searchIndex = [];

// The one headless command the site teaches. Run from the project root; jm run passes the
// environment through to the engine, which it finds beside itself.
const HEADLESS = `JM_HEADLESS=1 JM_SAVE_DIR=.jm-save \\
JM_EXIT_AFTER_FRAMES=600 JM_INPUT_REPLAY=replay.txt \\
JM_CAPTURE_DIR=frames JM_CAPTURE_FRAMES=120,300,590 \\
  jm run`;

// ---------------------------------------------------------------- layout

const NAV = [
  ["Get started", "start/"],
  ["Agent workflow", "agents/"],
  ["Games", "games/"],
  ["Docs", "docs/"],
  ["Editor", "editor/"],
  ["Download", "download/"],
];

function layout({ url, title, description, body, section }) {
  const depth = url === "" ? 0 : url.split("/").filter(Boolean).length;
  const root = depth ? "../".repeat(depth) : "./";
  const r = (u) => root + u;
  const current = (u) => (section === u ? ' aria-current="page"' : "");
  const navLinks = NAV.map(([label, u]) => `<a href="${r(u)}"${current(u)}>${label}</a>`).join("");
  const pageTitle = title ? `${title} | Journeyman` : "Journeyman Engine";
  return `<!doctype html>
<html lang="en" data-root="${root}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(pageTitle)}</title>
<meta name="description" content="${esc(description)}">
<meta property="og:title" content="${esc(pageTitle)}">
<meta property="og:description" content="${esc(description)}">
<meta property="og:image" content="${r("img/og.jpg")}">
<meta name="theme-color" content="#f4f5f7" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#18191c" media="(prefers-color-scheme: dark)">
<link rel="icon" href="${r("img/icon.png")}">
<script>try{var t=localStorage.getItem("jm-theme");if(t&&t!=="system")document.documentElement.dataset.theme=t}catch(e){}</script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Geist:wght@400;500;600&family=Geist+Mono:wght@400;500&display=swap" rel="stylesheet">
<link rel="stylesheet" href="https://unpkg.com/@phosphor-icons/web@2.1.1/src/regular/style.css">
<link rel="stylesheet" href="${r("css/site.css")}?v=${ASSET_V.css}">
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="nav">
  <div class="wrap">
    <a class="brand" translate="no" href="${root}"><img src="${r("img/icon.png")}" alt="" width="22" height="22">Journeyman</a>
    <nav class="nav-links" aria-label="Primary">${NAV.slice(1).map(([label, u]) => `<a href="${r(u)}"${current(u)}>${label}</a>`).join("")}</nav>
    <div class="nav-tools">
      <button class="search-btn" type="button" data-search-open aria-label="Search the site">${icon("magnifying-glass")}<span class="label">Search</span><kbd data-mod-k>Ctrl K</kbd></button>
      <button class="icon-btn" type="button" data-theme-toggle aria-label="Change theme">${icon("circle-half")}</button>
      <a class="icon-btn" href="${GH}" aria-label="Journeyman on GitHub">${icon("github-logo")}</a>
      <a class="btn btn-primary btn-sm" href="${r("start/")}"${current("start/")}>Get started</a>
      <button class="icon-btn menu-btn" type="button" data-menu-toggle aria-expanded="false" aria-controls="mobile-menu" aria-label="Menu">${icon("list")}</button>
    </div>
  </div>
  <nav class="mobile-menu" id="mobile-menu" aria-label="Mobile">${navLinks}</nav>
</header>
<main id="main">
${body}
</main>
<footer class="site">
  <div class="wrap">
    <div>
      <a class="brand" translate="no" href="${root}"><img src="${r("img/icon.png")}" alt="" width="22" height="22">Journeyman</a>
      <p>A small 2D game engine for building games with your agent. Free and MIT licensed, made by Patrick Burris.</p>
    </div>
    <div><p class="mini">Learn</p><ul><li><a href="${r("start/")}">Get started</a></li><li><a href="${r("agents/")}">Agent workflow</a></li><li><a href="${r("docs/")}">Documentation</a></li></ul></div>
    <div><p class="mini">Make</p><ul><li><a href="${r("games/")}">Games</a></li><li><a href="${r("editor/")}">Editor</a></li><li><a href="${r("download/")}">Download</a></li></ul></div>
    <div><p class="mini">Project</p><ul><li><a href="${GH}">GitHub</a></li><li><a href="${GH}/issues">Issues</a></li><li><a href="${GH}/releases">Releases</a></li><li><a href="${GH}/blob/master/LICENSE">License</a></li></ul></div>
  </div>
</footer>
<dialog id="search" aria-label="Search">
  <div class="palette">
    <div class="palette-input">${icon("magnifying-glass")}<input type="text" role="combobox" aria-expanded="false" aria-autocomplete="list" aria-controls="search-results" placeholder="Search docs, games and guides" aria-label="Search" autocomplete="off" spellcheck="false"></div>
    <ul id="search-results" role="listbox" aria-label="Results"></ul>
    <div class="palette-foot"><span><kbd>↑</kbd> <kbd>↓</kbd> to move</span><span><kbd>Enter</kbd> to open</span><span><kbd>Esc</kbd> to close</span><span class="visually-hidden" role="status" aria-live="polite" data-search-status></span></div>
  </div>
</dialog>
<dialog id="lightbox" class="lightbox" aria-label="Screenshot viewer">
  <img alt="">
  <div class="lb-bar"><button class="lb-prev" type="button" aria-label="Previous">${icon("arrow-left")}</button><span class="lb-cap" aria-live="polite"></span><button class="lb-next" type="button" aria-label="Next">${icon("arrow-right")}</button></div>
  <button class="lb-close" type="button" aria-label="Close">${icon("x")}</button>
</dialog>
<div class="visually-hidden" role="status" aria-live="polite" data-announce></div>
<script src="${r("js/search-index.js")}?v=${ASSET_V.search}"></script>
<script src="${r("js/site.js")}?v=${ASSET_V.js}"></script>
</body>
</html>`;
}

const pages = [];
function page(url, opts) {
  pages.push({ url, ...opts });
  if (opts.search !== false) searchIndex.push({ k: "page", t: opts.title || "Home", p: opts.description, u: url, x: "" });
}

// Paths in page bodies are written relative to the site root and prefixed per page.
const rootOf = (url) => (url === "" ? "./" : "../".repeat(url.split("/").filter(Boolean).length));
const R = (url) => (u) => rootOf(url) + u;

const code = (text) => `<div class="code-block"><pre translate="no"><code>${text}</code></pre></div>`;
// A prompt to paste into an agent: plain wrapped text with an always-visible copy button.
const copyPromptBtn = `<button class="copy-prompt" type="button" data-copy-prompt>${icon("copy")}<span>Copy prompt</span></button>`;
const promptBlock = (text, title = "Prompt") =>
  `<div class="code-block prompt no-copy"><div class="code-title"><span>${esc(title)}</span>${copyPromptBtn}</div><pre><code>${esc(text)}</code></pre></div>`;
// Dims comments in shell snippets and marks jm. Input is plain text.
const sh = (s) => esc(s).replace(/(^|\s)(#[^\n]*)/g, '$1<span class="c">$2</span>').replace(/(^|[\s;]|&amp;&amp; )jm /gm, '$1<span class="k">jm</span> ');

// <img> with width/height read from the file, so the browser reserves space before it loads,
// and a 640px srcset candidate when one sits beside it (name-640.jpg).
function imgSize(file) {
  const buf = fs.readFileSync(file);
  if (buf[0] === 0x89) return [buf.readUInt32BE(16), buf.readUInt32BE(20)]; // PNG
  for (let i = 2; i < buf.length;) {                                        // JPEG: find a SOFn marker
    const marker = buf[i + 1], len = buf.readUInt16BE(i + 2);
    if (marker >= 0xc0 && marker <= 0xcf && ![0xc4, 0xc8, 0xcc].includes(marker)) return [buf.readUInt16BE(i + 7), buf.readUInt16BE(i + 5)];
    i += 2 + len;
  }
  return null;
}
function img(src, alt, { r, cls = "pixel", lazy = true, style = "", priority = false, sizes = "" } = {}) {
  const file = path.join(here, "src", src);
  const size = fs.existsSync(file) ? imgSize(file) : null;
  const dims = size ? ` width="${size[0]}" height="${size[1]}"` : "";
  const small = src.replace(/\.jpg$/, "-640.jpg");
  const srcset = size && sizes && fs.existsSync(path.join(here, "src", small)) ? ` srcset="${r(small)} 640w, ${r(src)} ${size[0]}w" sizes="${sizes}"` : "";
  return `<img class="${cls}" src="${r(src)}"${srcset} alt="${esc(alt)}"${dims}${priority ? ' fetchpriority="high"' : lazy ? ' loading="lazy"' : ""}${style ? ` style="${style}"` : ""}>`;
}


// An image with a light-theme twin (name-light.jpg beside it) renders both in one box; CSS shows the
// one matching the page's theme, so switching themes swaps the picture in place.
function themedImg(src, alt, opts) {
  const lightSrc = src.replace(/\.jpg$/, "-light.jpg");
  if (!fs.existsSync(path.join(here, "src", lightSrc))) return img(src, alt, opts);
  const cls = opts.cls || "";
  return `<span class="themed ${cls}"${opts.style ? ` style="${opts.style}"` : ""}>${img(src, alt, { ...opts, cls: "for-dark", style: "" })}${img(lightSrc, alt, { ...opts, cls: "for-light", style: "", priority: false })}</span>`;
}

// ---------------------------------------------------------------- data

// Pixel-art captures scale crisp; painted ones (painted: true) scale smooth.
const shotClass = (g) => (g.painted ? "" : "pixel");

const GAMES = [
  {
    slug: "strike-wing", dir: "strike_wing", name: "Strike Wing 1942", kind: "Vertical shooter",
    shots: ["An orange bomber fires a fan of bullets at the player's plane", "A bomber explodes among enemy bullets", "A squadron of red fighters dives in from the top", "An enemy plane takes a hit and bursts into sparks"],
    line: "Three stages, the last a flying fortress.",
    about: "A 1942-style shooter: a title menu, three stages ending in a three-phase flying fortress in a storm, results screens between stages, game over with continue, a victory screen with credits, a pause menu, a saved high score and options for volume, CRT filter and fullscreen.",
    controls: "Arrows or WASD move, Space or Z fire, X bomb, Esc or P pause. Gamepads work too.",
    files: [["director.ts", "Stage timeline, waves, music and banners"], ["player.ts", "Movement, weapon levels, bombs, respawn"], ["enemy.ts", "Every regular enemy, driven by params"], ["boss.ts", "The three-phase boss"], ["lib/waves.ts", "Formations and stage schedules"]],
    ask: "Add a fourth weapon level that fires a wide spread. Run stage one headless and show me frames 300 and 600 with it.",
  },
  {
    slug: "ash-and-iron", dir: "Ash and Iron", name: "Ash and Iron", kind: "Turn-based RPG",
    shots: ["Exploring the town of Cinderwell", "Sella talks about the low cistern", "Walking the Ashen Road by a river crossing", "A turn-based fight with a Scrap Drone in the Foundry"],
    line: "Grid combat, quests and dialogue from data tables.",
    about: "A top-down RPG slice in the spirit of the old isometric Fallouts, made in the editor: painted maps, prefabs, and every item, ability, enemy, quest and line of dialogue entered as data. Three save slots, the town of Cinderwell, the Ashen Road and the Foundry, where the Iron Warden waits.",
    controls: "Click to walk, talk, search and fight, or use arrows or WASD and E. In a fight each step costs an action point; Space ends the turn.",
    files: [["assets/data/", "Items, abilities, enemies, people, dialogue, quests, the shop"], ["scenes/", "Title, intro, Cinderwell, Ashen Road, Foundry, ending"], ["scripts/game.ts", "Exploring, talking, menus, the shop and fights"], ["lib/state.ts", "The story so far, plus dialogue conditions and actions"], ["lib/grid.ts", "Paths and line of sight"]],
    ask: "Add a quest in Cinderwell where the scrapyard owner asks for three drone parts. Put the dialogue in the data tables.",
  },
  {
    slug: "aldane", dir: "jrpg", name: "Embers of Aldane", kind: "JRPG slice",
    shots: ["The village elder warns that the Cinder Wyrm has woken", "Kael crossing the tall grass of the Emberwood", "A battle against two slimes with the command menu open", "The Cinder Wyrm's Cinder Breath hits the party"],
    line: "Active-time battles, a party of three, saves.",
    about: "A slice of a Final Fantasy or Chrono Trigger style RPG. The village of Aldane and the Emberwood, villagers, chests, an inn and a shop, random encounters behind a swirl transition, active-time battles for Kael, Lyra and Bram, and the Cinder Wyrm waiting in its lair.",
    controls: "Arrows move, Z confirms or talks, X goes back, Esc opens the party menu.",
    files: [["lib/battle.ts", "Battle rules: gauges, commands, damage, statuses, AI (tested)"], ["lib/party.ts", "Levels, items, flags, save and load (tested)"], ["assets/data/bestiary.json", "Enemies and encounters as data"], ["scenes/town.scene.json", "The village, authored: map, people, chests"], ["scenes/test_boss.scene.json", "Starts the wyrm fight directly"]],
    ask: "Give Lyra an ice spell the Cinder Wyrm is weak to, and add a jm test that checks the damage.",
  },
  {
    slug: "hollow-grove", dir: "dungeon", name: "The Legend of Hollow Grove", kind: "Top-down adventure",
    shots: ["The hermit between two fires: the grove has gone quiet", "Wren swings the sword at blue slimes beside a pond", "Slimes close in on Wren in the grove", "Wren facing slimes in the crypt"],
    line: "Rooms, a sword, keys and locked doors.",
    about: "An action adventure in the spirit of the first Zelda: a four-room overworld and a six-room crypt with screen-by-screen scrolling, a hermit who hands you the sword, slimes, bats and skeletons, keys and locked doors, heart containers, and Ogloth guarding the shard.",
    controls: "Arrows move, Z swings the sword or talks, Esc pauses.",
    files: [["maps/grove.tmj", "The overworld as a Tiled map"], ["scripts/area.ts", "Scrolls between rooms and swaps each room in"], ["scripts/hero.ts", "Wren: movement, sword, doors, stairs, damage"], ["lib/foe.ts", "What enemies share: health, knockback, drops"], ["scenes/test_*.scene.json", "Start anywhere with any items, for testing"]],
    ask: "Add a room east of the grove with two bats and a key behind a pushable block.",
  },
  {
    slug: "super-pip", dir: "platformer", name: "Super Pip", kind: "Side-scroller",
    shots: ["Pip on a row of bricks and question blocks in World 1-1", "Pip stomps enemies underground in World 1-2", "Pip jumps among blue pillars topped with coins", "Pip leaps over lava pits in the World 1-3 castle"],
    line: "Three levels, power-ups and a flagpole finish.",
    about: "A platformer with an overworld, an underground and a castle. Question blocks and breakable bricks, a mushroom power-up, gloops and beetles with kickable shells, lava, a three-hit boss, a level clock, lives and a HUD. All art and music are original and generated by a script.",
    controls: "Arrows move, Z or Space jumps (hold for higher), X runs, Esc pauses.",
    files: [["maps/1-*.tmj", "The levels as Tiled maps"], ["lib/body.ts", "A TileBody with gravity: walls, floors, head bumps, lava"], ["lib/walker.ts", "How enemies and items move"], ["scripts/pip.ts", "The player, and the authority for every interaction"], ["scripts/level.ts", "Clock, HUD, music, pause, level flow"]],
    ask: "Add a fourth level at night that reuses the overworld tiles, with a darker palette shader.",
  },
  {
    slug: "neon-vow", dir: "neon_vow", name: "Neon Vow", kind: "Platformer", painted: true,
    shots: ["Kage hangs from a cable over the pit, a shrine lantern behind", "Kage leaps off the cable toward the far bank", "Running a mossy swale below a one-way ledge", "A sentry patrols before the torii gate"],
    line: "Hills drawn as lines, a cable to swing on, sentries to stomp.",
    about: "Kage, a cybernetic samurai, crosses a ruined neon city at dusk. The ground is drawn as lines, not tiles: rolling hills, eroded banks, one-way ledges. Swing across a pit on a hanging cable, stomp patrol sentries, light a shrine lantern checkpoint and reach the torii gate. Every image and the map come from one Python script, and a driven headless run checks the whole route.",
    controls: "Arrows run, Space or Z jumps (tap to hop, hold for full height), Down and Jump drops through a ledge. Touch a cable's end in the air to grab it; arrows pump the swing, Jump lets go.",
    files: [["tools/gen_art.py", "Paints every image and writes the map's ground lines"], ["assets/maps/level1.tmj", "Ground and ledges as lines, markers for cables, sentries and the gate"], ["scripts/hero.ts", "Running, jumps, the cable, stomps, checkpoints, the camera"], ["lib/moves.ts", "Movement rules, tested by jm test"], ["tools/check_level.py", "Drives the whole route headless: zero falls"]],
    ask: "Add a second cable over a wider pit after the gate, and extend check_level.py to swing across it.",
  },
  {
    slug: "tetris", dir: "tetris", name: "Tetris", kind: "Puzzle",
    shots: ["A stack with a well open on the right and the ghost piece showing", "A four-line clear: TETRIS!", "A mid-height stack with the next T piece about to drop", "The LEVEL 2 banner after a level-up"],
    line: "Guideline rules with rule tests.",
    about: "Guideline-style Tetris: the 7-bag, SRS rotation with wall kicks, hold, ghost piece, lock delay, a level speed curve, line-clear flash, high score, pause and game over. The rules live in one file with no input, sound or drawing, so they are tested directly.",
    controls: "Arrows move, Up or X rotates, Z rotates back, Down soft drops, Space hard drops, Shift or C holds, Esc pauses.",
    files: [["lib/game.ts", "The rules: no input, sound or drawing"], ["lib/pieces.ts", "Shapes, colors, SRS kick tables"], ["lib/well.ts", "Draws the board over the HTML layout"], ["tests/game.spec.ts", "Rule tests run by jm test"], ["scenes/puzzle_*.scene.json", "Preset boards for testing by eye"]],
    ask: "Add a sprint mode: clear 40 lines as fast as possible. Write the rule tests first.",
  },
  {
    slug: "pellet-party", dir: "pellet_party", name: "Pellet Party", kind: "Party game", multiplayer: "Peer to peer, 2–4 players",
    shots: ["Three players spread across the arena among green pellets", "Blue, red and green players in a shared round", "The three players moving away from their starting positions", "More pellets fill the arena as the round clock counts down"],
    line: "Eat pellets, share scores and keep playing when the host leaves.",
    about: "A multiplayer example for two to four players connected peer to peer. Each player simulates their own avatar, the host runs the pellets and round clock, and shared scores follow the session. If the host leaves, another player takes over.",
    controls: "Arrows or WASD move, Enter selects, Esc leaves.",
    files: [["assets/scripts/player.ts", "Owner-simulated avatars"], ["assets/scripts/pellet.ts", "Host-simulated pellets and eating messages"], ["assets/scripts/director.ts", "Round clock and shared scores"], ["assets/prefabs/player.prefab.json", "Player prefab with owner authority"], [".jm.json", "Peer-to-peer session configuration"]],
    ask: "Add a bonus pellet worth five points, with the host awarding its score once.",
  },
  {
    slug: "tank-arena", dir: "tank_arena", name: "Tank Arena", kind: "Tank combat", multiplayer: "Dedicated server, up to 8 players",
    shots: ["Two tanks and crates in the server-simulated arena", "A tank fires across the arena", "Two players steer their tanks around the arena", "Tank combat with shared armor and scores"],
    line: "Server-simulated tanks, shared scores and server-only bots.",
    about: "A multiplayer example with up to eight tanks on a dedicated server. The server simulates tanks from each player's input and decides hits. Rules and bots run only on the server; clients display shared armor, scores and the kill feed.",
    controls: "Left and right turn, up and down drive, Space fires, Esc leaves.",
    files: [["assets/scripts/tank.ts", "Tanks simulated from their players' input"], ["assets/scripts/shell.ts", "Shells and hit messages"], ["assets/scripts/server/rules.ts", "Server-only match rules and bots"], ["assets/scripts/hud.ts", "Shared armor, scores and kill feed"], [".jm.json", "Dedicated server configuration"]],
    ask: "Add a server-controlled armor pickup and show each tank's updated armor in the HUD.",
  },
  {
    slug: "checkers", dir: "checkers", name: "Checkers", kind: "Board game", multiplayer: "Matchmaker, then peer to peer, 2 players",
    shots: ["The shared board at the start of an online match", "Red selects a piece and legal moves are highlighted", "Red chooses a destination for the opening move", "The board after the players have made opening moves"],
    line: "Find a match, connect directly and play a host-judged board.",
    about: "A multiplayer example of online American checkers. A dedicated matchmaker pairs two players, then they connect directly using NAT punching. The host checks moves on a shared board, including compulsory captures, capture chains and kings. The rules have unit tests.",
    controls: "Arrows select a square and Enter moves, or click. Red moves first; Esc leaves.",
    files: [["assets/scripts/server/matchmaker.ts", "Pairs players and hands off connection addresses"], ["assets/scripts/title.ts", "Matchmaking and the peer-to-peer hand-off"], ["assets/scripts/board.ts", "Shared board and host-judged moves"], ["assets/scripts/lib/rules.ts", "American checkers rules"], ["tests/rules.spec.ts", "Rule tests run by jm test"]],
    ask: "Show all legal moves for the selected piece, including compulsory captures, and test the rules.",
  },
];

const GAME_COUNT = GAMES.length;
const MULTIPLAYER_COUNT = GAMES.filter((g) => g.multiplayer).length;

function gameCard(g, r, opts = {}) {
  const preview = `<div class="frame screen">${img(`img/games/${g.slug}/1.jpg`, `${g.name}: ${g.shots[0]}`, { r, cls: shotClass(g), ...opts })}</div>`;
  const label = g.multiplayer ? `Multiplayer example · ${g.kind}` : g.kind;
  return `<a class="game-card" href="${r(`games/${g.slug}/`)}">${preview}<b>${esc(g.name)}</b><span>${esc(label)}. ${esc(g.line)}</span></a>`;
}

const DOCS = [
  { slug: "scripting", file: "scripting.md", title: "Scripting API", summary: "Everything @jm/runtime gives your scripts: entities, spawning, input, audio, UI, scenes, rendering, time and saves." },
  { slug: "gameplay", file: "runtime-gameplay.md", title: "Gameplay building blocks", summary: "Projectiles, timers and timelines, menus and HUDs, sessions, settings and screens." },
  { slug: "content", file: "content.md", title: "Content formats", summary: "The .jm.json manifest, scenes, prefabs, components, atlases, tile maps, UI, shaders, input and audio." },
  { slug: "testing", file: "testing.md", title: "Testing and automation", summary: "Unit tests, jm test, deterministic headless runs, input replay and frame capture." },
  { slug: "networking", file: "networking.md", title: "Multiplayer", summary: "Sessions, shared entities, dedicated servers from the same game, matchmaking, and testing sessions on one machine." },
  { slug: "plays", file: "plays.md", title: "Plays", summary: "Every jm run is recorded: look at a moment, mark what you mean with F8, hand it to your agent, replay or resume it." },
  { slug: "performance", file: "performance.md", title: "Performance", summary: "How engine changes are measured (A/B runs of two trees), the frame budgets, and the results so far." },
  { slug: "glossary", file: "glossary.md", title: "Glossary", summary: "Journeyman's names next to Godot, Unity, Box2D and Tiled, and what differs." },
  { slug: "cli", file: "cli.md", title: "The jm CLI", summary: "Every jm command and flag, generated from jm's own help." },
  { slug: "editor", file: "editor.md", title: "Editor", summary: "The workspace, scene editing, tile painting, asset editors, play, export, shortcuts and automation." },
];

// ---------------------------------------------------------------- shared blocks

// Install, by OS. Ends with jm --version: making a project is its own step.
function installTabs(id) {
  const tab = (v, label) => `<button role="tab" type="button" id="${id}-t-${v}" data-value="${v}" aria-controls="${id}-p-${v}">${label}</button>`;
  const panel = (v, body) => `<div role="tabpanel" id="${id}-p-${v}" aria-labelledby="${id}-t-${v}">${body}</div>`;
  const unix = (profile, platform) => code(sh((hasAsset("install.sh") ? `# installs the CLI (jm and the engine) in ~/.jm/bin
curl -fsSL \\
  ${asset("install.sh")} | sh
` : `# download the CLI (jm and the engine) and put it in ~/.jm/bin${platform === "darwin-arm64" ? "\n# (Intel Mac: platform=darwin-amd64)" : ""}
platform=${platform}
curl -fsSLO ${RELEASE ? `${GH}/releases/download/${RELEASE.tag}/` : `${GH}/releases/latest/download/`}journeyman-cli-$platform.tar.gz
tar -xzf journeyman-cli-$platform.tar.gz
mkdir -p ~/.jm && rm -rf ~/.jm/bin && mv journeyman-cli-$platform ~/.jm/bin
`) + `
# put it on PATH, now and for new shells
export PATH="$HOME/.jm/bin:$PATH"
echo 'export PATH="$HOME/.jm/bin:$PATH"' >> ${profile}
jm --version`));
  const win = hasAsset("install.ps1") ? code(`<span class="c"># PowerShell: installs the CLI in ~\\.jm\\bin and adds it to PATH</span>
irm ${asset("install.ps1")} | iex
<span class="k">jm</span> --version`) : code(`<span class="c"># PowerShell: download, unzip, add to PATH for new shells too</span>
Invoke-WebRequest ${asset("journeyman-cli-windows-amd64.zip")} -OutFile jm.zip
Expand-Archive jm.zip $HOME\\.jm
$bin = "$HOME\\.jm\\journeyman-cli-windows-amd64"
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";$bin", "User")
$env:Path += ";$bin"
<span class="k">jm</span> --version`);
  const quarantine = `<p class="tab-note">Downloaded the tarball in a browser instead? macOS quarantines it. Clear that once:</p>${code(sh(`xattr -dr com.apple.quarantine journeyman-cli-darwin-arm64`))}`;
  return `<div class="tabs" data-sync="os"><div role="tablist" aria-label="Operating system">${tab("macos", "macOS")}${tab("linux", "Linux")}${tab("windows", "Windows")}</div>${panel("macos", unix("~/.zshrc", "darwin-arm64") + quarantine)}${panel("linux", unix("~/.bashrc", "linux-amd64"))}${panel("windows", win + `<p class="tab-note">Windows SmartScreen may warn the first time. Choose More info, then Run anyway.</p>`)}<p class="detected"></p></div>`;
}

// ---------------------------------------------------------------- home

{
  const url = "", r = R(url);
  const framesFile = path.join(here, "src/img/scrub/frames.json");
  const frames = fs.existsSync(framesFile) ? JSON.parse(fs.readFileSync(framesFile, "utf8")) : Array.from({ length: 24 }, (_, i) => 30 + i * 30);
  const replayFile = path.join(here, "src/img/scrub/replay.txt");
  const replayLines = fs.readFileSync(replayFile, "utf8").split("\n").filter((l) => /^\d+ /.test(l) && +l.split(" ")[0] <= frames.at(-1));
  // Markers on the scrubber track for the inputs that change what you see.
  const span = frames.at(-1) - frames[0];
  const markers = [[100, "Enter"], [160, "Space"]]
    .map(([f, label]) => `<span class="marker" style="left:${(((f - frames[0]) / span) * 100).toFixed(2)}%" title="${label} pressed at frame ${f}"></span>`).join("");
  const strip = GAMES.map((g) => gameCard(g, r, { sizes: "(max-width: 900px) 50vw, 400px" })).join("");
  page(url, {
    title: "", section: "", description: "A small 2D game engine for building games with your agent. Plain files, one CLI, headless runs that give the same frames every time.",
    body: `
<section class="hero hero-home">
  <div class="wrap">
    <div class="hero-copy">
      <h1 class="display rise" style="--i:0">Work with your agent to build 2D games.</h1>
      <p class="lede rise" style="--i:1">You describe the game. Your agent writes it as plain files, then builds, tests and plays it to check its work.</p>
      <div class="ctas rise" style="--i:2">
        ${agentButton("btn btn-primary")}
        <a class="btn btn-ghost" href="${r("start/")}">${icon("terminal-window")}Install it yourself</a>
      </div>
      <p class="rise cta-note" style="--i:2">Paste it into Claude, Codex or any agent that runs commands: it installs Journeyman${hasAsset("install.ps1") ? " and the editor, connects itself" : ""} and makes your first game.</p>
    </div>
    <figure class="scrub rise" style="--i:2" data-scrub data-base="${r("img/scrub/")}" data-frames='${JSON.stringify(frames)}'>
      <div class="scrub-screen screen"><img class="pixel" src="${r("img/scrub/f00.jpg")}" alt="Strike Wing at frame ${frames[0]}: the title screen" width="480" height="640" fetchpriority="high"></div>
      <div class="scrub-controls">
        <div class="scrub-row">
          <button class="icon-btn" type="button" data-play aria-pressed="false" aria-label="Play through the frames">${icon("play")}</button>
          <div class="track"><input type="range" min="0" max="${frames.length - 1}" value="0" step="1" aria-label="Frame" aria-valuetext="Frame ${frames[0]}">${markers}</div>
        </div>
        <figcaption><span class="scrub-readout"><span>Frame <b data-frame>${frames[0]}</b> of ${frames.at(-1)}</span><span data-file>frame_00030.png</span></span><span class="scrub-note">One headless run. The ticks mark the replayed keys: Enter at frame 100, then Space held from 160.</span></figcaption>
      </div>
    </figure>
  </div>
</section>

<section class="block block-tight" id="loop">
  <div class="wrap">
    <h2 class="h2 reveal">Your agent can play what it builds</h2>
    <div class="how reveal">
      <p class="sub">Those ${frames.length} frames came from one run with a hidden window, a fixed timestep, seed 1 and key presses read from a file. Run it again and you get the same frames, byte for byte. CI plays every demo twice and fails if one frame differs. That is what lets your agent check its own work and show you the result. It can also <a class="text-link" href="${r("agents/")}#drive">drive the game</a> a step at a time: wait for the lift to reach the top, ask what the player is touching, draw the colliders into a frame.</p>
      ${code(sh(`# from the project root: 720 frames, every 30th saved
JM_HEADLESS=1 JM_SAVE_DIR=.jm-save \\
JM_EXIT_AFTER_FRAMES=720 \\
JM_INPUT_REPLAY=replay.txt \\
JM_CAPTURE_DIR=frames \\
JM_CAPTURE_FRAMES=30,60,90,...,720 \\
  jm run`))}
      ${code(sh(`# replay.txt
${replayLines.slice(0, 7).join("\n")}
# ...`))}
    </div>
  </div>
</section>

<section class="band">
  <div class="wrap">
    <h2 class="h2 reveal">One change, start to finish</h2>
    <p class="sub reveal" style="margin-bottom:40px">An example of a session in the Strike Wing project, with Claude Code, Codex or any agent that can edit files, run commands and read images.</p>
    <div class="session reveal">
      <div>
        <h3>You ask</h3>
        <p class="ask">“The boss is too easy in its last phase. Make the ring attack denser and a bit faster, then show me before and after.”</p>
      </div>
      <div>
        <h3>Your agent edits boss.ts</h3>
        ${code(`  } else if (attackCount % 2 == 0) {
<span class="del">-   ringShot.ring(body.x, body.y - 10, 18,</span>
<span class="add">+   ringShot.ring(body.x, body.y - 10, 24,</span>
      Random.range(0, PI));
<span class="del">-   attackTimer.start(1.0);</span>
<span class="add">+   attackTimer.start(0.85);</span>`)}
      </div>
      <div>
        <h3>Then checks it</h3>
        <ul class="checks">
          <li>${icon("check")}<span>Builds with jm build</span></li>
          <li>${icon("check")}<span>Starts at the boss stage with JM_ENTRY_SCENE</span></li>
          <li>${icon("check")}<span>Captures the ring attack before and after</span></li>
          <li>${icon("check")}<span>Opens both frames and shows you</span></li>
        </ul>
      </div>
    </div>
  </div>
</section>

<section class="block">
  <div class="wrap">
    <h2 class="h2 reveal">The whole game is in files you both can read</h2>
    <p class="sub reveal" style="margin-bottom:40px">Scenes and prefabs are JSON. Gameplay is AssemblyScript. Screens are a small subset of HTML and CSS. Nothing hides in a binary project file.</p>
    <div class="files reveal">
      ${code(`strike_wing/
  .jm.json
  scenes/
    title.scene.json
    level1.scene.json
    level2.scene.json
    boss.scene.json
  assets/
    prefabs/boss.prefab.json
    scripts/
      boss.ts
      player.ts
      victory.ts
    ui/title.ui.html
    shaders/crt.frag`)}
      ${code(`<span class="c">// assets/scripts/victory.ts (trimmed)</span>
<span class="k">import</span> { Audio, Input, Music, UI, blink } <span class="k">from</span> "@jm/runtime";
<span class="c">// ...</span>

const music = new Music("music_title");
UI.setText("score", scoreText(Session.score.value));
Audio.play("jingle_victory");

<span class="k">export function</span> onUpdate(dt: f32): void {
  t += dt;
  if (t &lt; 2.0) return;
  UI.opacity("prompt", blink(t, 2, 1, 0.4));
  if (Input.justPressed("confirm")) {
    music.fadeOut(0.8);
    screen.goTo("title");
  }
}`)}
    </div>
  </div>
</section>

<section class="block">
  <div class="wrap">
    <h2 class="h2 reveal">${GAME_COUNT} games to start from</h2>
    <p class="sub reveal" style="margin-bottom:40px">Each is a complete project in the repo, including ${MULTIPLAYER_COUNT} multiplayer examples. Clone it, open one with your agent and ask for a new level.</p>
    <div class="games-strip reveal">${strip}</div>
    <p class="reveal" style="margin-top:28px"><a class="arrow-link" href="${r("games/")}">See all the games ${icon("arrow-right")}</a></p>
  </div>
</section>

<section class="block">
  <div class="wrap split">
    <div class="reveal">
      <h2 class="h2">Step in whenever you want</h2>
      <p class="sub">Paint a map, nudge a value or play the level in the editor. It saves to the same files, so your agent picks up your change on its next build.</p>
      <p style="margin-top:24px"><a class="arrow-link" href="${r("editor/")}">Tour the editor ${icon("arrow-right")}</a></p>
    </div>
    ${themedImg("img/editor/tiles-crop.jpg", "Painting a pond onto the Hollow Grove map in the editor", { r, cls: "editor-shot reveal" })}
  </div>
</section>

<section class="block closing">
  <div class="wrap split">
    <div class="reveal">
      <h2 class="h2">Get it running</h2>
      <p class="sub">Install the CLI, make a project, then open the folder with your agent. Free and MIT licensed.</p>
      <div class="ctas" style="margin-top:28px"><a class="btn btn-primary" href="${r("start/")}">${icon("terminal-window")}Get started</a></div>
    </div>
    <div class="reveal">${installTabs("home")}</div>
  </div>
</section>`,
  });
}

// ---------------------------------------------------------------- reading pages (start, agents)

// A reading page: content column with a table of contents built from its h2s, and an optional "next" pager.
function readingPage(url, { title, description, section, intro, sections, next, top = "" }) {
  const toc = sections.map((s) => `<li><a href="#${s.id}">${esc(s.title)}</a></li>`).join("");
  const body = sections.map((s) => `<h2 id="${s.id}">${esc(s.title)}<a class="anchor" href="#${s.id}" aria-label="Link to ${esc(s.title)}">#</a></h2>\n${s.html}`).join("\n");
  sections.forEach((s) => searchIndex.push({ k: "section", t: s.title, p: title, u: `${url}#${s.id}`, x: unesc(s.html.replace(/<[^>]+>/g, " ")).replace(/\s+/g, " ") }));
  const pager = next ? `<nav class="pager" aria-label="Next steps">${next.map(([label, href, hint], i) => `<a class="${i ? "next" : ""}" href="${href}"><small>${hint}</small>${label}</a>`).join("")}</nav>` : "";
  page(url, {
    title, description, section,
    body: `<div class="wrap reading no-side">
  <article class="prose">
    <h1>${esc(title)}</h1>
    <p class="lead">${intro}</p>
    ${top}
    ${body}
    ${pager}
  </article>
  <aside class="toc" aria-label="On this page"><p class="mini">On this page</p><ul>${toc}</ul></aside>
</div>`,
  });
}

{
  const url = "start/", r = R(url);
  readingPage(url, {
    title: "Get started", section: url,
    top: `<div class="agent-callout"><p>Rather let your agent do it? Copy this prompt into Claude Code, Codex or any agent that can run commands. It installs everything, makes your first project and checks it runs.</p>${agentButton("btn btn-primary")}<a class="arrow-link" href="${r("agents/install/")}">Read what it does ${icon("arrow-right")}</a></div>`,
    description: "Install the Journeyman CLI, make your first project and open it with your agent.",
    intro: "Install the CLI, make a project, open it with your agent and ask for something you can see. Every step is a command you can paste.",
    sections: [
      { id: "before", title: "Before you start", html: `<dl class="faq">
<div><dt>What it costs</dt><dd>Nothing. Journeyman is free and MIT licensed. Your agent's usage is billed by its own provider.</dd></div>
<div><dt>Platforms</dt><dd>macOS (Apple silicon and Intel), Linux x64 and Windows x64. The engine needs OpenGL 4.1. This is an early release; anything may change before 1.0.</dd></div>
<div><dt>Which agents</dt><dd>Any agent that can edit files, run commands and read images, so it can look at the frames it captures. Claude Code and Codex both can.</dd></div>
<div><dt>A display</dt><dd>Headless runs hide the window but still render with OpenGL. On a Linux server or in a sandbox without a display, wrap the command in xvfb-run, as the project's CI does.</dd></div>
</dl>` },
      { id: "node", title: "Install Node.js", html: `<p>Game scripts are AssemblyScript and compile with Node.js 20 or newer. jm build installs the compiler into each project the first time it runs.</p>${code(sh(`node --version   # v20 or newer`))}` },
      { id: "install", title: "Install the CLI", html: `<p>The CLI is jm plus the engine it runs. It is what your agent uses.</p>${installTabs("start")}` },
      { id: "project", title: "Make a project", html: `<p>A project is a folder you own. Everything in it is a source file; jm writes everything it generates to build/.</p>${code(sh(`mkdir my-game && cd my-game
jm init "My Game"   # sets up this folder; the name is the game's
jm build
jm run`))}
<p>Then save the agent map below as AGENTS.md (or CLAUDE.md) in the project folder, so your agent knows where things are and how to check its work.</p>
${code(agentsMd())}` },
      { id: "agent", title: "Ask your agent for something you can see", html: `<p>Open the project folder in your agent and paste this. A new project has no art yet, so the first ask uses a plain shape.</p>
${promptBlock(`Read AGENTS.md. Add a player: a 16 by 16 white square in the middle of the screen that moves with the arrow keys. Build it, write a replay that holds the right arrow from frame 30 to frame 120, run it headless and show me frames 30 and 120.`, "First prompt")}
<p>When that works, ask for real art, a second scene or a score. The <a href="${r("agents/")}">agent workflow</a> page covers replays, test scenes and rule tests.</p>` },
      { id: "editor", title: "Open the editor, if you like", html: `<p>The editor is optional. It opens the same folder, shows your scenes the way the engine draws them and plays the game in a panel. Anything you change there is a file your agent sees on its next build.</p><p><a class="arrow-link" href="${r("download/")}">Download the editor ${icon("arrow-right")}</a></p>` },
    ],
    next: [["Agent workflow", r("agents/"), "Next"], ["Pick a game to start from", r("games/"), "Or"]],
  });
}

function agentsMd() {
  return sh(`# This project is a Journeyman game

- Gameplay: assets/scripts/*.ts (AssemblyScript, imports from @jm/runtime).
  API: https://github.com/Jumballaya/Journeyman-Engine/blob/master/docs/scripting.md
- Scenes: scenes/*.scene.json. Prefabs: assets/prefabs/*.prefab.json.
- Screens: assets/ui/*.ui.html and .css. Shaders: assets/shaders/*.frag.
- Never edit build/. jm build regenerates it.

## Checking your work
- Build: jm build. Rule tests: jm test (tests/*.spec.ts).
- To look at the game, write a replay (one "frame down|up Key" per line),
  run it headless from the project root, then open the PNGs in frames/:
  ${HEADLESS.replace(/\n/g, "\n  ")}
- Show me the frames when you finish a visual change.`);
}

{
  const url = "agents/", r = R(url);
  readingPage(url, {
    title: "Agent workflow", section: url,
    top: `<div class="agent-callout"><p>Rather let your agent do it? Copy this prompt into Claude Code, Codex or any agent that can run commands. It installs everything, makes your first project and checks it runs.</p>${agentButton("btn btn-primary")}<a class="arrow-link" href="${r("agents/install/")}">Read what it does ${icon("arrow-right")}</a></div>`,
    description: "How to build games with an AI agent in Journeyman: the loop, the project map, deterministic headless runs, driving the game, recorded plays, input replay, frame capture and tests.",
    intro: "Journeyman is built so your agent can do the whole loop on its own: change a file, build, run the game, look at the result. Your part is to say what the game should be and judge what you see.",
    sections: [
      { id: "loop", title: "The loop", html: `<div class="loop">
<h3>You ask</h3><p>Describe the change in terms of the game: what the player sees and does.</p>
<h3>Your agent edits files</h3><p>Scripts, scenes, prefabs, UI screens and data. No editor needed.</p>
<h3>It builds and tests</h3><p>jm build compiles scripts and bakes atlases. jm test runs rule tests without rendering.</p>
<h3>It plays the game</h3><p>A headless run with scripted input, saving the frames it wants to see.</p>
<h3>It shows you</h3><p>It opens the frames, checks them against what you asked and shows you before and after.</p>
</div>` },
      { id: "map", title: "Give your agent a map", html: `<p>Agents work faster with a short note about where things are. Save this as AGENTS.md or CLAUDE.md at the root of your project and adjust it as the game grows.</p>
${code(agentsMd())}` },
      { id: "headless", title: "Run the game headless", html: `<p>Run from the project root. jm run finds the engine beside itself and passes these variables through to it.</p>
${code(sh(HEADLESS))}
<p>An automated run steps a fixed 1/60 s with seed 1. The same build, replay and seed give the same frames, byte for byte, as long as each run starts from an empty save folder.</p>
<table><thead><tr><th>Variable</th><th>Effect</th></tr></thead><tbody>
<tr><td><code>JM_HEADLESS=1</code></td><td>Hide the window. Rendering still happens.</td></tr>
<tr><td><code>JM_SAVE_DIR=dir</code></td><td>Saves go here, not to the player's save folder.</td></tr>
<tr><td><code>JM_EXIT_AFTER_FRAMES=n</code></td><td>Quit cleanly after n frames.</td></tr>
<tr><td><code>JM_INPUT_REPLAY=file</code></td><td>Play key presses from a file. Real input is ignored.</td></tr>
<tr><td><code>JM_CAPTURE_DIR</code>, <code>JM_CAPTURE_FRAMES</code></td><td>Write the listed frames as PNG files.</td></tr>
<tr><td><code>JM_ENTRY_SCENE=scenes/x.scene.json</code></td><td>Start in another scene.</td></tr>
<tr><td><code>JM_SESSION=file.json</code></td><td>Session values set before the first frame. With JM_ENTRY_SCENE, a deep link: the boss, with one life.</td></tr>
<tr><td><code>JM_SEED=n</code></td><td>The random seed. A run you play by hand logs its seed so you can repeat it.</td></tr>
<tr><td><code>JM_FIXED_DT=s</code></td><td>Change the fixed step from 1/60 s.</td></tr>
</tbody></table>
<p class="note">Headless frames are not held to the display rate, so runs finish faster than real time. The engine logs the average frame time on exit. Without a display, wrap the command in xvfb-run.</p>` },
      { id: "replay", title: "Script the input", html: `<p>A replay file has one key event per line: the frame, down or up, and the key. Lines starting with # are comments.</p>
${code(sh(`# replay.txt
100 down Enter
103 up Enter
160 down Space      # hold fire from frame 160
190 down ArrowLeft
230 up ArrowLeft`))}
<p>Ask your agent to keep a replay for each thing worth checking: reaching the boss, opening the pause menu, losing a life. They become cheap regression checks.</p>` },
      { id: "drive", title: "Drive the game", html: `<p>With JM_DRIVE=1 the game only moves when told. Your agent sends commands on stdin and reads one JSON answer per line, so it can play, ask and look in one run.</p>
${code(sh(`printf 'step 60\npress Enter\nuntil tag=Player VelocityComponent.onGround == true max 300\nnear tag=Player 20\ndebug physics on\ncapture shot.png\nquit\n' \\
  | JM_DRIVE=1 JM_HEADLESS=1 jm run`))}
<table><thead><tr><th>Command</th><th>Answers</th></tr></thead><tbody>
<tr><td><code>step [n]</code>, <code>down|up|press Key</code></td><td>Advance frames, hold or tap keys.</td></tr>
<tr><td><code>get</code>, <code>state</code></td><td>A component's value, or the whole game state: entities, session, UI, the draw list.</td></tr>
<tr><td><code>until tag=Lift TransformComponent.y &lt; -270 max 600</code></td><td>Steps until it's true, so the agent waits for the thing, not a guessed frame count.</td></tr>
<tr><td><code>near tag=Player 20</code></td><td>What is within 20 units of it, nearest first: why a pickup or a landing doesn't touch.</td></tr>
<tr><td><code>debug physics on</code></td><td>Draws colliders and ground into the frames from now on.</td></tr>
</tbody></table>
<p>JM_DRIVE_RECORD=file keeps the run's input as a replay, so whatever the agent found becomes a check that runs again. Every command is in the <a class="text-link" href="${r("docs/testing/")}">testing docs</a>.</p>` },
      { id: "plays", title: "Show it what you mean", html: `<p>Every time you play with jm run, the play is recorded: your input, the state every half second, and thumbnails. Press F8 when something looks off, then tell your agent in your own words: "at my marker the jump felt floaty". It replays your play to that moment, exactly, and looks.</p>
${code(sh(`jm plays show                    # what happened: scenes, values over time, your markers
jm plays frame latest m1         # what you saw at marker 1, replayed exactly
jm plays state latest m1 session # the numbers then
jm plays verify                  # after a fix: does your play go differently now?
jm plays resume latest m1        # play on from that moment yourself`))}
<p>More in <a class="text-link" href="${r("docs/plays/")}">Plays</a>.</p>` },
      { id: "start-anywhere", title: "Start anywhere", html: `<p>Playing to the boss every time is slow. Three of the demos keep test scenes that jump straight to a moment: Hollow Grove's take an area, a position and the items you hold, and Embers of Aldane has one that starts the wyrm fight. In Strike Wing you can start at the boss stage directly.</p>
${code(sh(`# in demos/jrpg
JM_ENTRY_SCENE=scenes/test_boss.scene.json jm run

# in demos/strike_wing
JM_ENTRY_SCENE=scenes/boss.scene.json jm run`))}` },
      { id: "tests", title: "Test the rules", html: `<p>jm test compiles tests/*.spec.ts and runs every exported function as a test. Game state and saves are in memory and data files are read from the project, so it suits rules, data and state. It needs no build.</p>
${code(`<span class="c">// tests/rules.spec.ts</span>
<span class="k">import</span> { Game } <span class="k">from</span> "../assets/scripts/lib/game";

<span class="k">export function</span> clearingFourLinesScoresATetris(): void {
  const game = new Game();
  <span class="c">// ...</span>
  assert(game.score == 800, "a tetris scores 800");
}`)}
<p>Keeping rules in their own file, with no input, sound or drawing, is what makes them testable. Tetris and Embers of Aldane are built that way.</p>` },
      { id: "ship", title: "Ship it", html: `<p>When the game is ready, jm packs it into one archive and appends that to a copy of the engine. Players get a single executable with nothing to install.</p>
${code(sh(`jm pack      # build/<name>.jm
jm export    # dist/<Name>.app on macOS, dist/<Name> on Linux, .exe on Windows
jm export --target windows-amd64 \\
  --player journeyman-engine-windows-amd64.exe`))}` },
    ],
    next: [["Testing and automation", r("docs/testing/"), "Reference"], ["Scripting API", r("docs/scripting/"), "Next"]],
  });
}

// ---------------------------------------------------------------- games

{
  const url = "games/", r = R(url);
  const cards = GAMES.map((g, i) => gameCard(g, r, { lazy: i >= 3, sizes: "(max-width: 900px) 100vw, 400px" })).join("");
  page(url, {
    title: "Games", section: url,
    description: `${GAME_COUNT} complete games built on Journeyman, including ${MULTIPLAYER_COUNT} multiplayer examples, each a project you can open with your agent and change.`,
    body: `<div class="wrap">
  <header class="page-head">
    <h1 class="title rise">${GAME_COUNT} games to start from</h1>
    <p class="sub rise" style="--i:1">Each was built to find what the engine lacked, and each is a complete project in the repo. The ${MULTIPLAYER_COUNT} multiplayer examples cover peer-to-peer play, dedicated servers and matchmaking. Clone it, pick one, open it with your agent and ask for something new.</p>
  </header>
  <div class="games-grid" style="padding-bottom:96px">${cards}</div>
</div>`,
  });
}

GAMES.forEach((g, i) => {
  const url = `games/${g.slug}/`, r = R(url);
  const prev = GAMES[(i + GAMES.length - 1) % GAMES.length], next = GAMES[(i + 1) % GAMES.length];
  const shots = [1, 2, 3, 4].map((n) => `<button class="screen" type="button" data-lightbox="shots" data-src="${r(`img/games/${g.slug}/${n}.jpg`)}" data-alt="${esc(g.shots[n - 1])}" aria-label="Enlarge: ${esc(g.shots[n - 1])}">${img(`img/games/${g.slug}/${n}.jpg`, g.shots[n - 1], { r, sizes: "(max-width: 900px) 50vw, 300px", cls: shotClass(g) })}</button>`).join("");
  const dir = g.dir.includes(" ") ? `"Journeyman-Engine/demos/${g.dir}"` : `Journeyman-Engine/demos/${g.dir}`;
  const files = g.files.map(([f, d]) => `<li><code>${esc(f)}</code><span>${esc(d)}</span></li>`).join("");
  page(url, {
    title: g.name, section: "games/",
    description: `${g.name}: ${g.line} A complete Journeyman project.`,
    body: `<div class="wrap">
  <nav class="crumbs" aria-label="Breadcrumb" style="padding-top:40px"><a href="${r("games/")}">Games</a> / ${esc(g.name)}</nav>
  <section class="game-hero">
    <button class="main-shot screen" type="button" data-lightbox="shots" data-src="${r(`img/games/${g.slug}/title.jpg`)}" data-alt="${esc(g.name)} title screen" aria-label="Enlarge the title screen">${img(`img/games/${g.slug}/title.jpg`, `${g.name} title screen`, { r, lazy: false, priority: true, cls: shotClass(g) })}</button>
    <div>
      <h1 class="title">${esc(g.name)}</h1>
      <p class="sub">${esc(g.about)}</p>
      <dl class="facts">
        <div><dt>Kind</dt><dd>${g.multiplayer ? "Multiplayer example · " : ""}${esc(g.kind)}</dd></div>
        ${g.multiplayer ? `<div><dt>Players</dt><dd>${esc(g.multiplayer)}</dd></div>` : ""}
        <div><dt>Controls</dt><dd>${esc(g.controls)}</dd></div>
        <div><dt>Source</dt><dd><a class="text-link" href="${GH}/tree/master/demos/${encodeURIComponent(g.dir)}">demos/${esc(g.dir)}</a></dd></div>
      </dl>
    </div>
  </section>
  <section style="padding-bottom:72px" aria-label="Screenshots">
    <div class="shot-row">${shots}</div>
  </section>
  <section class="block made">
    <div>
      <h2 class="h2">Try it with your agent</h2>
      <p class="sub" style="margin-bottom:20px">Clone the repo and run it once, then open the folder in your agent and ask for something like:</p>
      ${promptBlock(g.ask)}
      <div style="height:16px"></div>
      ${code(sh(`git clone --depth 1 ${GH}.git
cd ${dir}
${g.multiplayer ? 'cd assets/scripts && npm install && cd ../..\njm build && jm run --peers 2' : 'jm build && jm run'}`))}
      ${g.multiplayer ? `<p>Launches two players on this machine. See <a class="text-link" href="${r("docs/networking/")}">the multiplayer guide</a> for sessions, servers and matchmaking.</p>` : ""}
    </div>
    <div>
      <h2 class="h2">How it is made</h2>
      <p class="sub" style="margin-bottom:12px">The files worth reading first.</p>
      <ul class="file-list">${files}</ul>
    </div>
  </section>
  <nav class="game-pager block" aria-label="More games" style="padding-bottom:96px">
    <a class="arrow-link" href="${r(`games/${prev.slug}/`)}">${icon("arrow-left")} ${esc(prev.name)}</a>
    <a class="arrow-link" href="${r(`games/${next.slug}/`)}">${esc(next.name)} ${icon("arrow-right")}</a>
  </nav>
</div>`,
  });
});

// ---------------------------------------------------------------- docs

const slugify = (s) => s.toLowerCase().replace(/<[^>]+>/g, "").replace(/&[a-z]+;/g, "").replace(/[^\w\s-]/g, "").trim().replace(/\s+/g, "-");
const docBySource = Object.fromEntries(DOCS.map((d) => [d.file, d]));
// Markdown to search text: no markup, code fences kept as words.
const plainText = (md) => md.replace(/```[a-z]*\n?/g, " ").replace(/[`*_#|>\[\]()]/g, " ").replace(/\s+/g, " ").trim();

const renderDoc = (doc) => renderMarkdown(fs.readFileSync(path.join(repo, "docs", doc.file), "utf8"));

function renderMarkdown(md) {
  const toc = [];
  const renderer = new marked.Renderer();
  renderer.heading = (text, level, raw) => {
    if (level === 1) return `<h1>${text}</h1>\n`;
    const id = slugify(raw);
    if (level <= 3) toc.push({ id, level, html: text.replace(/<[^>]+>/g, "") });
    return `<h${level} id="${id}">${text}<a class="anchor" href="#${id}" aria-label="Link to this section">#</a></h${level}>\n`;
  };
  renderer.code = (text, lang) => {
    let html = esc(text);
    if (/^(sh|bash|zsh|shell)?$/.test(lang || "")) html = html.replace(/(^|\s)(#[^\n]*)/g, '$1<span class="c">$2</span>');
    if (/^(ts|js|typescript|cpp|c\+\+|glsl|frag)$/.test(lang || "")) html = html.replace(/(\/\/[^\n]*)/g, '<span class="c">$1</span>');
    return `<pre translate="no"><code>${html}</code></pre>\n`;
  };
  renderer.link = (href, title, text) => {
    let h = href || "";
    const [file, hash] = h.split("#");
    if (docBySource[file]) h = `../${docBySource[file].slug}/${hash ? "#" + hash : ""}`;
    else if (file && !/^[a-z]+:/.test(file)) h = `${GH}/blob/master/${path.posix.normalize("docs/" + file)}${hash ? "#" + hash : ""}`;
    return `<a href="${esc(h)}">${text}</a>`;
  };
  renderer.image = (href, title, text) => `<img src="${esc(/^[a-z]+:/.test(href) ? href : `https://raw.githubusercontent.com/Jumballaya/Journeyman-Engine/master/docs/${href}`)}" alt="${esc(text)}" loading="lazy">`;
  const html = marked.parse(md, { renderer, gfm: true });
  // Search entries: one per h2 and h3, holding the full text under that heading.
  const sections = [];
  for (const chunk of md.split(/^(?=#{2,3} )/m).slice(1)) {
    const [head, ...rest] = chunk.split("\n");
    const raw = head.replace(/^#{2,3} /, "");
    sections.push({ id: slugify(raw), title: raw.replace(/`/g, ""), text: plainText(rest.join("\n")) });
  }
  return { html, toc, sections };
}

const rendered = DOCS.map((d) => ({ ...d, ...renderDoc(d) }));

function docsSidebar(r, active) {
  return `<nav class="side" aria-label="Documentation"><p class="mini">Guides</p><ul><li><a href="${r("start/")}">Get started</a></li><li><a href="${r("agents/")}">Agent workflow</a></li></ul><p class="mini">Reference</p><ul>${rendered.map((d) =>
    `<li><a href="${r(`docs/${d.slug}/`)}"${d.slug === active ? ' aria-current="page"' : ""}>${esc(d.title)}</a></li>`).join("")}</ul></nav>`;
}

{
  const url = "docs/", r = R(url);
  page(url, {
    title: "Documentation", section: url,
    description: "Journeyman documentation: scripting API, gameplay building blocks, content formats, testing and the editor.",
    body: `<div class="wrap reading" style="grid-template-columns: 220px minmax(0,1fr)">
  ${docsSidebar(r, "")}
  <div>
    <h1 class="title">Documentation</h1>
    <p class="sub" style="margin-bottom:40px">The same docs that live in the repository. Link your agent to them from AGENTS.md.</p>
    <div class="doc-list">${rendered.map((d) => `<a href="${r(`docs/${d.slug}/`)}"><b>${esc(d.title)}</b><span>${esc(d.summary)}</span></a>`).join("")}</div>
    <p style="margin-top:40px" class="muted">Press <kbd data-mod-k>Ctrl K</kbd> to search every section.</p>
  </div>
</div>`,
  });
}

rendered.forEach((d, i) => {
  const url = `docs/${d.slug}/`, r = R(url);
  const prev = rendered[i - 1], next = rendered[i + 1];
  d.sections.forEach((s) => searchIndex.push({ k: "doc", t: s.title, p: d.title, u: `${url}#${s.id}`, x: s.text }));
  const toc = d.toc.map((t) => `<li class="lvl-${t.level}"><a href="#${t.id}">${t.html}</a></li>`).join("");
  page(url, {
    title: d.title, section: "docs/", description: d.summary,
    body: `<div class="wrap reading">
  ${docsSidebar(r, d.slug)}
  <article class="prose">
    <nav class="crumbs" aria-label="Breadcrumb"><a href="${r("docs/")}">Docs</a> / ${esc(d.title)}</nav>
    ${d.html}
    <nav class="pager" aria-label="Previous and next">
      ${prev ? `<a href="${r(`docs/${prev.slug}/`)}"><small>Previous</small>${esc(prev.title)}</a>` : "<span></span>"}
      ${next ? `<a class="next" href="${r(`docs/${next.slug}/`)}"><small>Next</small>${esc(next.title)}</a>` : ""}
    </nav>
  </article>
  <aside class="toc" aria-label="On this page"><p class="mini">On this page</p><ul>${toc}</ul><a class="toc-edit" href="${GH}/edit/master/docs/${d.file}">${icon("pencil-simple")}Edit on GitHub</a></aside>
</div>`,
  });
});

// ---------------------------------------------------------------- agent install guide

const AGENT_GUIDE_MD = agentInstallGuide({ release: RELEASE, gh: GH, site: SITE });
{
  const url = "agents/install/", r = R(url);
  const guide = renderMarkdown(AGENT_GUIDE_MD);
  guide.toc.filter((t) => t.level === 2).forEach((t) => searchIndex.push({ k: "section", t: unesc(t.html), p: "Install guide for agents", u: `${url}#${t.id}`, x: "" }));
  page(url, {
    title: "Install guide for agents", section: "agents/",
    description: "Step-by-step install and first-project setup for an AI agent: platform, Node.js, the Journeyman CLI, a first game, AGENTS.md and a headless check.",
    body: `<div class="wrap reading no-side">
  <article class="prose">
    <nav class="crumbs" aria-label="Breadcrumb"><a href="${r("agents/")}">Agent workflow</a> / Install guide for agents</nav>
    <div class="agent-callout">
      <p>This page is written for your agent, not for you. Copy the prompt, paste it into your agent, and it reads this guide and does every step: install, first project, a check that it runs. Agents can also read it as plain text at <a href="${r("agents/install.md")}">agents/install.md</a>.</p>
      ${agentButton("btn btn-primary")}
    </div>
    ${guide.html.replace(/<h1>[\s\S]*?<\/h1>/, "<h1>Install guide for agents</h1>")}
  </article>
  <aside class="toc" aria-label="On this page"><p class="mini">On this page</p><ul>${guide.toc.filter((t) => t.level === 2).map((t) => `<li><a href="#${t.id}">${t.html}</a></li>`).join("")}</ul></aside>
</div>`,
  });
}

// ---------------------------------------------------------------- editor

{
  const url = "editor/", r = R(url);
  const shortcuts = [
    ["Command palette", "Ctrl K"], ["Save", "Ctrl S"], ["Undo / Redo", "Ctrl Z / Ctrl Shift Z"], ["Duplicate", "Ctrl D"],
    ["Select, move, rotate, scale", "Q W E R"], ["Brush, rectangle, fill", "B U G"], ["Eraser, picker", "X I"], ["Frame selection", "F"],
    ["Play scene", "Ctrl P or F6"], ["Play game", "F5"], ["Step one frame", "F10"], ["Build / Export", "Ctrl B / Ctrl Shift E"],
  ].map(([a, k]) => `<div><span>${a}</span><kbd>${k}</kbd></div>`).join("");
  const shot = (file, alt) => {
    const light = fs.existsSync(path.join(here, `src/img/editor/${file}-light.jpg`)) ? ` data-src-light="${r(`img/editor/${file}-light.jpg`)}"` : "";
    return `<button class="shot-btn" type="button" data-lightbox="editor" data-src="${r(`img/editor/${file}.jpg`)}"${light} data-alt="${esc(alt)}" aria-label="Enlarge: ${esc(alt)}">${themedImg(`img/editor/${file}.jpg`, alt, { r, cls: "" })}</button>`;
  };
  page(url, {
    title: "Editor", section: url,
    description: "The Journeyman editor: paint maps, edit data and screens, play in place. It saves to the same files your agent works on.",
    body: `<section class="hero" style="padding-bottom:48px">
  <div class="wrap" style="grid-template-columns:1fr">
    <div style="max-width:760px">
      <h1 class="display rise">Paint, tune and play the same files</h1>
      <p class="lede rise" style="--i:1;max-width:54ch">The editor opens the folder your agent is working in. Paint a map, tune a value, play the level. Every change is an ordinary project file, so your agent picks it up on its next build.</p>
      <div class="ctas rise" style="--i:2"><a class="btn btn-primary" href="${r("download/")}?kind=editor">${icon("download-simple")}Download the editor</a><a class="btn btn-ghost" href="${r("docs/editor/")}">Read the editor docs</a></div>
    </div>
  </div>
</section>
<div class="wrap">
  ${themedImg("img/editor/scene.jpg", "The Embers of Aldane town in the editor's Scene view, the Elder selected and its components in the inspector", { r, cls: "editor-shot rise", style: "--i:3", priority: true })}
</div>
<section class="block" style="margin-top:96px">
  <div class="wrap">
    <div class="ed-feature reveal">
      <div class="ed-text"><h2 class="h3">Paint tile maps</h2><p>Brush, rectangle, fill and picker tools over Tiled maps, with edge-aware auto-tiling. The maps stay Tiled files your agent can read and write.</p></div>
      ${shot("tiles-crop", "Painting a pond onto the Hollow Grove map with the tile palette open")}
    </div>
    <div class="ed-feature flip reveal">
      <div class="ed-text"><h2 class="h3">Play it in place</h2><p>Play runs the game inside the editor. Pause, step one frame, and inspect the live entities while it runs. Play sessions never touch a player's saves.</p></div>
      ${shot("play-crop", "Embers of Aldane mid-battle in the Game panel, with the running entities listed")}
    </div>
  </div>
</section>
<section class="band">
  <div class="wrap">
    <h2 class="h2 reveal">Every file has an editor</h2>
    <p class="sub reveal" style="margin-bottom:40px">Data tables, UI screens, tilesets, atlases, input actions and shaders open as tabs, and save themselves a moment after you stop typing.</p>
    <figure class="wide-shot reveal">${shot("data-crop", "Ash and Iron's items table open in the data editor, Old Revolver selected")}<figcaption>Ash and Iron's items, quests and dialogue are all data tables like this one.</figcaption></figure>
    <figure class="wide-shot reveal">${shot("ui-crop", "Strike Wing's title screen in the UI editor, a menu item selected")}<figcaption>Screens are HTML and CSS, edited in place.</figcaption></figure>
  </div>
</section>
<section class="block">
  <div class="wrap split" style="align-items:start">
    <div class="reveal"><h2 class="h2">Keyboard shortcuts</h2><p class="sub">Ctrl means Cmd on macOS. Help, Keyboard Shortcuts lists every one.</p></div>
    <div class="shortcuts reveal">${shortcuts}</div>
  </div>
</section>
<section class="block" style="padding-bottom:120px">
  <div class="wrap split">
    <div class="reveal"><h2 class="h2">Export a game in one click</h2><p class="sub">Export builds one executable with the engine and every asset inside: an app on macOS, an exe on Windows, a single binary on Linux.</p></div>
    <div class="reveal">${code(sh(`# the same thing from a terminal, or from your agent
jm export
jm export --target windows-amd64 \\
  --player journeyman-engine-windows-amd64.exe`))}</div>
  </div>
</section>`,
  });
}

// ---------------------------------------------------------------- download

{
  const url = "download/", r = R(url);
  const rows = [
    ["journeyman-cli-darwin-arm64.tar.gz", "CLI", "macOS, Apple silicon"], ["journeyman-cli-darwin-amd64.tar.gz", "CLI", "macOS, Intel"],
    ["journeyman-cli-linux-amd64.tar.gz", "CLI", "Linux x64"], ["journeyman-cli-windows-amd64.zip", "CLI", "Windows x64"],
    ["journeyman-editor-darwin-arm64.zip", "Editor", "macOS, Apple silicon"], ["journeyman-editor-darwin-amd64.zip", "Editor", "macOS, Intel"],
    ["journeyman-editor-linux-amd64.tar.gz", "Editor", "Linux x64"], ["journeyman-editor-windows-amd64.zip", "Editor", "Windows x64"],
    ["journeyman-engine-&lt;platform&gt;", "Engine", "Exporting games to another platform"], ["install.sh", "Script", "The one-line installer for macOS and Linux"],
    ["SHA256SUMS", "Checksums", "SHA-256 of every file"],
  ].map(([f, k, p]) => `<tr data-file="${f}"><td>${f.includes("&lt;") ? f : `<a class="text-link" href="${asset(f)}">${f}</a>`}</td><td>${k}</td><td>${p}</td></tr>`).join("");
  page(url, {
    title: "Download", section: "download/",
    description: "Download the Journeyman CLI (jm and the engine), or the editor with both inside, for macOS, Linux and Windows.",
    body: `<div class="wrap" data-download data-dl-base="${RELEASE ? `${GH}/releases/download/${RELEASE.tag}/` : ""}" data-dl-assets="${RELEASE ? [...RELEASE.assets].join(" ") : ""}" data-dl-releases="${RELEASES}">
  <header class="page-head">
    <h1 class="title rise">Download Journeyman</h1>
    <p class="sub rise" style="--i:1">${RELEASE ? `Version ${esc(RELEASE.tag)}. ` : ""}Free and MIT licensed. An early release: anything may change before 1.0. For the CLI, the <a class="text-link" href="${r("start/")}#install">terminal install</a> is quickest.</p>
  </header>
  <fieldset class="picker rise" style="--i:2">
    <legend class="visually-hidden">What to download</legend>
    <div class="seg" role="radiogroup" aria-label="What to download">
      <label><input type="radio" name="kind" value="cli" checked><span><b>CLI</b> jm and the engine, for you and your agent</span></label>
      <label><input type="radio" name="kind" value="editor"><span><b>Editor</b> the desktop editor, with the CLI inside</span></label>
    </div>
  </fieldset>
  <div class="platform-picker rise" style="--i:3" role="group" aria-label="Platform">
    <button type="button" data-os="macos" aria-pressed="true">Mac (Apple)</button><button type="button" data-os="macos-intel" aria-pressed="false">Mac (Intel)</button><button type="button" data-os="linux" aria-pressed="false">Linux</button><button type="button" data-os="windows" aria-pressed="false">Windows</button>
  </div>
  <p class="detected" data-dl-detected></p>
  <div class="dl-main rise" style="--i:4">
    <a class="btn btn-primary" data-dl-btn href="${asset("journeyman-cli-darwin-arm64.tar.gz")}">${icon("download-simple")}<span data-dl-label>Download for macOS, Apple silicon</span></a>
    <span class="mono" data-dl-file>journeyman-cli-darwin-arm64.tar.gz</span>
  </div>

  <section class="block" style="margin-top:72px">
    <h2 class="h2" style="margin-bottom:24px">Every file</h2>
    <div class="table-wrap"><table class="files-table"><thead><tr><th>File</th><th>What</th><th>For</th></tr></thead><tbody>${rows}</tbody></table></div>
    <p class="muted" style="margin-top:20px;font-size:15px">Older versions and release notes are on <a class="text-link" href="${GH}/releases">GitHub Releases</a>.</p>
  </section>

  <section class="block">
    <div class="split" style="align-items:start">
      <div>
        <h2 class="h2">If your system blocks it</h2>
        <p class="sub">The builds are not notarized by Apple or signed for Windows yet.</p>
      </div>
      <div class="prose">
        <p>On macOS, a browser download can be reported as damaged or from an unidentified developer. Clear the quarantine flag once:</p>
        ${code(sh(`xattr -dr com.apple.quarantine "/Applications/Journeyman Editor.app"
xattr -dr com.apple.quarantine journeyman-cli-darwin-arm64`))}
        <p>On Windows, SmartScreen may warn the first time. Choose More info, then Run anyway.</p>
        <p>To check a download against the published checksums:</p>
        ${code(sh(`# macOS
shasum -a 256 -c SHA256SUMS --ignore-missing
# Linux
sha256sum -c SHA256SUMS --ignore-missing`))}
      </div>
    </div>
  </section>

  <section class="block" style="padding-bottom:120px">
    <div class="split" style="align-items:start">
      <div>
        <h2 class="h2">Build from source</h2>
        <p class="sub">CMake 3.25+ and Ninja, a C++23 compiler, Go 1.24+, Node.js 20+ and OpenGL 4.1. CMake fetches the rest.</p>
      </div>
      ${code(sh(`git clone ${GH}.git && cd Journeyman-Engine

# the engine and the editor
./scripts/build-release.sh

# the CLI
(cd cli && go build -o ../build/bin/jm ./cmd/jm)

# build and play Strike Wing
./scripts/play-demo.sh`))}
    </div>
  </section>
</div>`,
  });
}

// ---------------------------------------------------------------- 404

page("404/", {
  title: "Page not found", section: "", search: false,
  description: "This page does not exist.",
  body: `<div class="wrap page-head" style="min-height:60vh">
  <h1 class="title">Nothing at this address</h1>
  <p class="sub" style="margin-bottom:28px">The page may have moved. Search for it, or start from one of these.</p>
  <div class="ctas"><button class="btn btn-primary" type="button" data-search-open>${icon("magnifying-glass")}Search</button><a class="btn btn-ghost" href="../">Home</a><a class="btn btn-ghost" href="../docs/">Docs</a></div>
</div>`,
});

// ---------------------------------------------------------------- write

fs.rmSync(out, { recursive: true, force: true });
fs.cpSync(path.join(here, "src"), out, { recursive: true });
for (const p of pages) {
  const dir = path.join(out, p.url);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "index.html"), layout(p));
}
// GitHub Pages serves 404.html at whatever path was missed, so relative links can't work there.
// Its links are rewritten against a base the page works out from its own URL before anything loads.
{
  const html = fs.readFileSync(path.join(out, "404/index.html"), "utf8")
    .replace('data-root="../"', 'data-root="./"')
    .replaceAll('"../', '"./')
    .replace("<head>", `<head>
<script>(function(){var p=location.pathname,m=p.match(/^\\/[^/]+\\//),gh=/\\.github\\.io$/.test(location.hostname);var b=gh&&m?m[0]:"/";document.write('<base href="'+b+'">');document.documentElement.dataset.root=b})();</script>`);
  fs.writeFileSync(path.join(out, "404.html"), html);
}
const decoded = searchIndex.map((e) => ({ ...e, t: unesc(e.t), p: unesc(e.p) }));
fs.writeFileSync(path.join(out, "js/search-index.js"), `window.JM_SEARCH=${JSON.stringify(decoded)};`);
fs.writeFileSync(path.join(out, ".nojekyll"), "");
fs.writeFileSync(path.join(out, "agents/install.md"), AGENT_GUIDE_MD);
// llms.txt: the plain-text entry points for agents (https://llmstxt.org).
fs.writeFileSync(path.join(out, "llms.txt"), `# Journeyman Engine

> A small 2D game engine for building games with an AI agent: games are plain files (JSON scenes, AssemblyScript scripts, HTML/CSS screens) that one CLI, jm, builds, runs headless and tests.

## Start here
- [Install and first-project guide for agents](${SITE}agents/install.md): install the CLI and engine, create a project, write AGENTS.md, check it runs

## Docs
${DOCS.map((d) => `- [${d.title}](https://raw.githubusercontent.com/Jumballaya/Journeyman-Engine/master/docs/${d.file}): ${d.summary}`).join("\n")}

## Examples
- [Demo games](${GH}/tree/master/demos): ${GAME_COUNT} complete projects, including ${MULTIPLAYER_COUNT} multiplayer examples
`);
console.log(`${pages.length} pages, ${searchIndex.length} search entries -> ${out}`);
