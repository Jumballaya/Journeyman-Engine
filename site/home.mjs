// The home page: Paper "Site v2 / Home · v2", built from the Working print design system.
// Captures are real (games, headless frames, the editor); the chat is a labelled illustration.
const svg = (viewBox, body, cls = "") => `<svg class="${cls}" viewBox="${viewBox}" fill="none" aria-hidden="true">${body}</svg>`;
const label = (text, cls = "") => `<span class="kicker ${cls}">${text}</span>`;

const chat = (project, turns) => `<figure class="chat-window">
  <div class="chat-chrome"><span class="dots" aria-hidden="true"><i></i><i></i><i></i></span><span>${project} · your agent</span><span></span></div>
  <div class="chat-body">${turns.map(([role, text]) => `<p class="chat-${role}">${text}</p>`).join("")}</div>
  <div class="chat-composer" aria-hidden="true"><span>Message your agent</span><span class="send">↑</span></div>
  <figcaption class="kicker">Illustrative exchange</figcaption>
</figure>`;

// Hero proof: the game sits where you and your agent overlap. Coordinates are the Paper artboard's,
// relative to the Venn's own box (780 × 510).
const venn = (r) => `<div class="venn">
  ${svg("0 0 780 510", `<circle cx="245" cy="255" r="245" fill="var(--sage)"/><circle cx="535" cy="255" r="245" fill="var(--slate)"/><path d="M390 57.5A245 245 0 0 1 390 452.5A245 245 0 0 1 390 57.5Z" fill="var(--clay)"/><g stroke="var(--rule)"><circle cx="245" cy="255" r="262"/><circle cx="535" cy="255" r="262"/><path d="M20 255H780M390 0V510"/></g>`, "venn-art")}
  <p class="venn-you">${label("You")}<span>Design it.<br>Steer it.</span></p>
  <p class="venn-agent">${label("Your agent")}<span>Build it.<br>Check it.</span></p>
  <figure class="venn-game"><a class="plate" href="${r("games/neon-vow/")}"><img src="${r("img/games/neon-vow/1-640.jpg")}" width="640" height="560" alt="Neon Vow, a painted platformer made with Journeyman"></a><figcaption>Your game</figcaption></figure>
</div>`;

const GAMES = [
  ["super-pip", "Super Pip", "Platformer", 560],
  ["ash-and-iron", "Ash and Iron", "Turn-based RPG · made in the editor", 360],
  ["tetris", "Tetris", "Puzzle", 853],
  ["aldane", "Embers of Aldane", "JRPG", 480],
  ["hollow-grove", "Hollow Grove", "Adventure", 560],
];

const TOOLS = [
  ["Tiled", ".tmj · .tsj", "Maps, layers, animated tiles and terrains. Open a map in Tiled from the editor."],
  ["Aseprite", "sheet .png + .json", "Export a sprite sheet with tags. Each tag plays as an animation."],
  ["HTML + CSS", ".ui.html", "Menus and HUDs in a supported subset: flexbox, absolute layout, selectors."],
  ["AssemblyScript", ".ts → wasm", "Gameplay scripts, edited in VS Code, Cursor, Zed or Sublime."],
  ["JSON + git", ".scene.json", "Scenes, prefabs and data tables, written so diffs stay small."],
];

export function homePage({ r, frames, promptCard, installTabs }) {
  const frame = (i) => r(`img/scrub/f${String(i).padStart(2, "0")}.jpg`);
  const MARK = 11; // the F8 marker: frame 360, the red ship
  const strip = frames.map((f, i) => `<button type="button" data-frame-index="${i}" aria-label="Show frame ${f}"${i === MARK ? ' aria-current="true"' : ""}><img class="pixel" src="${frame(i)}" width="480" height="640" alt="" loading="lazy"></button>`).join("");
  return `<div class="home">
<section class="home-hero wrap" aria-labelledby="home-title">
  <h1 id="home-title">Work with your agent <br>to build 2D games.</h1>
  <div class="hero-start">
    <p class="hero-sub">Journeyman is a free, open-source 2D game engine that your agent can drive from the terminal. Describe the game you want, and it comes back as plain files, a build and frames you can play and judge.</p>
    ${promptCard({ note: `Paste into Claude Code or Codex. It installs Journeyman and the editor, then makes your first game. <a href="${r("agents/install/")}">What it does</a>` })}
    <div class="hero-download"><a class="btn btn-ghost" href="${r("download/")}"><i class="ph ph-download-simple" aria-hidden="true"></i><span>Download</span></a><span>Early release · macOS, Windows, Linux · MIT</span></div>
  </div>
  ${venn(r)}
</section>

<section class="games-strip wrap" aria-labelledby="games-title">
  <header class="section-head"><h2 id="games-title">Ten games to open and change.</h2><a class="arrow-link" href="${r("games/")}">See all ten <span aria-hidden="true">→</span></a></header>
  <div class="contact-sheet">${GAMES.map(([slug, name, kind, h]) => `<a href="${r(`games/${slug}/`)}" style="--ratio:${(640 / h).toFixed(3)}"><img class="pixel" src="${r(`img/games/${slug}/1-640.jpg`)}" width="640" height="${h}" alt="${name}" loading="lazy"><b>${name}</b>${label(kind)}</a>`).join("")}</div>
</section>

<section class="agent-plays" aria-labelledby="plays-title"><div class="wrap">
  <div class="plays-copy">
    ${label("Build · test · play")}
    <h2 id="plays-title">Your agent can play.</h2>
    <p>Your agent builds the game, runs it headless with recorded input and saves the frames it wants to look at. A headless run steps at a fixed rate with a fixed seed, so after a change your agent can replay the same input and compare.</p>
    <a class="arrow-link" href="${r("agents/")}">See the agent workflow <span aria-hidden="true">→</span></a>
  </div>
  <div class="plays-proof">
    <div class="code"><div class="code-title"><span>Your agent’s terminal · Strike Wing</span></div><pre translate="no"><code><span class="c">$</span> jm build --json
{"result":"ok"}
<span class="c">$</span> JM_HEADLESS=1 JM_INPUT_REPLAY=replay.txt \\
  JM_CAPTURE_FRAMES=180,360,600 jm run
saved .jm/frames/frame_00180.png
saved .jm/frames/frame_00360.png
saved .jm/frames/frame_00600.png</code></pre></div>
    <div class="captures">${[[5, 180], [11, 360], [19, 600]].map(([i, f]) => `<figure><img class="pixel" src="${frame(i)}" width="480" height="640" alt="Strike Wing at frame ${f}" loading="lazy"><figcaption class="kicker">Frame ${f}</figcaption></figure>`).join("")}</div>
  </div>
</div></section>

<section class="replay" aria-labelledby="replay-title"><div class="wrap">
  <div class="replay-disc" aria-hidden="true"></div>
  <header class="replay-head">
    <div>${label("F8 · recorded play")}<h2 id="replay-title">Express Yourself.</h2></div>
    <p>“That part’s too hard.” Every play is recorded. Press F8 at the moment you mean, and your agent can replay it, read the game’s state and try a fix.</p>
  </header>
  <div id="recorded-play" class="console" data-scrub data-initial="${MARK}" data-base="${r("img/scrub/")}" data-frames='${JSON.stringify(frames)}'>
    <div class="console-top">
      <div class="preview">
        <div class="plate scrub-screen"><img class="pixel" src="${frame(MARK)}" alt="Strike Wing at frame ${frames[MARK]}" width="480" height="640"><span class="replay-marker-leader" aria-hidden="true"></span></div>
        <p class="replay-marker-note">${label("F8 · your marker")}<span>tick <span data-frame>${frames[MARK]}</span> of ${frames.at(-1)}</span></p>
      </div>
      ${chat("Strike Wing", [["you", "I marked it with F8. Give me a little more room to dodge here."], ["agent", "I’ll replay your marker, widen the gap between the bullets and show you frame 360 again."]])}
    </div>
    <div class="filmstrip">${strip}</div>
    <div class="track" style="--mark:${MARK / (frames.length - 1)}"><input type="range" min="0" max="${frames.length - 1}" value="${MARK}" step="1" aria-label="Recorded play frame" aria-valuetext="Frame ${frames[MARK]} of ${frames.at(-1)}"><span class="f8-tick" aria-hidden="true">F8</span></div>
    <div class="controls">
      <div><button class="icon-btn" type="button" data-prev-frame aria-label="Previous frame"><i class="ph ph-skip-back" aria-hidden="true"></i></button><button class="icon-btn" type="button" data-play aria-pressed="false" aria-label="Play through the frames"><i class="ph ph-play" aria-hidden="true"></i></button><button class="icon-btn" type="button" data-next-frame aria-label="Next frame"><i class="ph ph-skip-forward" aria-hidden="true"></i></button><span class="readout" data-frame-readout>${frames[MARK]} / ${frames.at(-1)}</span><span class="visually-hidden" data-file>frame_00360.png</span></div>
      <button type="button" class="arrow-link marker-jump" data-jump-frame="${MARK}">Jump to F8 marker</button>
    </div>
    <p class="console-note">In ChatGPT and other apps that show MCP widgets, your agent can open this timeline right in the chat.</p>
  </div>
</div></section>

<section class="tools wrap" aria-labelledby="tools-title">
  <header class="section-head"><h2 id="tools-title">Use the tools you’re used to.</h2><p>A Journeyman game is a folder of ordinary files. Draw maps in Tiled, animate in Aseprite, write code in your own editor. Your agent reads and edits the same files.</p></header>
  <div class="tool-plates">
    <figure><img src="${r("img/editor/tiled.png")}" width="1527" height="599" alt="Tiled's map editor showing its Super Catboy example" loading="lazy"><figcaption>${label("Tiled · its Super Catboy example")}<a class="kicker" href="https://www.mapeditor.org/">Image: mapeditor.org</a></figcaption></figure>
    <figure><img src="${r("img/editor/hollow-grove-home.jpg")}" width="1495" height="785" alt="Hollow Grove open in the Journeyman editor" loading="lazy"><figcaption>${label("Journeyman editor · optional")}${label("Hollow Grove")}</figcaption><p>Select an object, copy its reference and paste it to your agent. When your agent edits the scene you have open, the editor merges its changes in. <a href="${r("editor/")}">The editor</a></p></figure>
  </div>
  <ul class="tool-tiles">${TOOLS.map(([name, ext, text]) => `<li><b>${name}</b><code>${ext}</code><span>${text}</span></li>`).join("")}</ul>
</section>

<section class="get-going" aria-labelledby="go-title"><div class="wrap">
  ${label("Get going", "on-sage")}
  <h2 id="go-title">Paste one prompt, or run one command.</h2>
  <div class="go-grid">
    ${promptCard({ clip: false, note: "Already installed? Paste it anyway. It connects your agent." })}
    <div class="go-self">${label("Install it yourself", "on-sage")}${installTabs("home")}</div>
  </div>
</div></section>
</div>`;
}
