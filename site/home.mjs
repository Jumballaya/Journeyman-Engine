// The home page: Paper file "Journeyman Design", page "Cobalt Manual", board Home.
// Captures are real (games, headless frames, the editor); the terminal and chat are labelled illustrations.
const kicker = (text, cls = "") => `<span class="kicker ${cls}">${text}</span>`;
const sticker = (who, text, cls) => `<p class="sticker ${cls}">${kicker(who)}<span>${text}</span></p>`;

const GAMES = [
  ["super-pip", "Super Pip", "Platformer", 560, true],
  ["ash-and-iron", "Ash and Iron", "Turn-based RPG, made in the editor", 360, true],
  ["tetris", "Tetris", "Puzzle", 853, true],
  ["neon-vow", "Neon Vow", "Painted platformer", 560, false],
  ["hollow-grove", "Hollow Grove", "Adventure", 560, true],
];

const LOOP = [
  ["01 · You", "Ask for a change", "Say what the player should see and do.", "you"],
  ["02 · Agent", "Edits plain files", "Scripts, scenes, prefabs, UI and data.", "agent"],
  ["03 · Agent", "Builds, tests, plays", "jm build, jm test and a headless run with recorded input.", "agent"],
  ["04 · You", "Play it and steer", "It shows you frames. You play and decide what’s next.", "you"],
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
  const MARK = 11; // the F8 marker: frame 360, the red plane
  const strip = frames.map((f, i) => `<button type="button" data-frame-index="${i}" aria-label="Show frame ${f}"${i === MARK ? ' aria-current="true"' : ""}><img class="pixel" src="${frame(i)}" width="480" height="640" alt="" loading="lazy"></button>`).join("");
  return `<div class="home">
<section class="home-hero" aria-labelledby="home-title">
  <div class="hero-copy">
    ${kicker("Free · open source · 2D engine")}
    <h1 id="home-title">Work with your agent to build 2D games.</h1>
    <p class="hero-sub">Journeyman is a game engine your agent can drive from the terminal. Describe the game, and it comes back as plain files, a build you can play, and frames you can judge.</p>
    ${promptCard({ note: `Your agent installs Journeyman and the editor, then makes your first game. <a href="${r("agents/install/")}">What it does</a> · <a href="${r("download/")}">Download instead</a>` })}
    <p class="tool-chips"><span>Works with</span>${TOOLS.map(([name]) => `<b>${name}</b>`).join("")}</p>
  </div>
  <div class="stage">
    ${sticker("You", "Design it. Steer it.", "s-you")}
    <figure class="stage-plate"><span class="shot"><img class="pixel" src="${frame(MARK)}" width="480" height="640" alt="Strike Wing at frame 360, the moment marked with F8"><span class="f8-ring" aria-hidden="true"></span><span class="f8-sticker kicker">F8 · you marked this</span></span><figcaption>Strike Wing, frame 360. Made with Journeyman.</figcaption></figure>
    ${sticker("Your agent", "Build it. Check it.", "s-agent")}
    <div class="term-card no-copy"><div class="code-title"><span>Your agent’s terminal</span><span class="illus">illustrative</span></div><pre translate="no"><code>$ jm plays frame latest m1          <span class="c"># your F8 mark</span>
$ jm build --json                   <span class="c">ok</span>
$ JM_HEADLESS=1 JM_CAPTURE_DIR=.jm/frames \\
    JM_CAPTURE_FRAMES=360 jm run
saved .jm/frames/frame_00360.png</code></pre></div>
  </div>
</section>

<section class="games-strip wrap" aria-labelledby="games-title">
  <header class="section-head reveal"><div>${kicker("10 example projects")}<h2 id="games-title">Ten games to open and change.</h2></div><a class="arrow-link" href="${r("games/")}">See all ten <span aria-hidden="true">→</span></a></header>
  <div class="contact-sheet">${GAMES.map(([slug, name, kind, h, pixel]) => `<a href="${r(`games/${slug}/`)}" style="--ratio:${(640 / h).toFixed(3)}"><img${pixel ? ' class="pixel"' : ""} src="${r(`img/games/${slug}/1-640.jpg`)}" width="640" height="${h}" alt="${name}" loading="lazy"><b>${name}</b><span>${kind}</span></a>`).join("")}</div>
</section>

<section class="agent-plays" aria-labelledby="plays-title"><div class="wrap">
  <header class="section-head reveal"><div>${kicker("Build · test · play")}<h2 id="plays-title">Your agent can play.</h2></div><p>It runs the game headless with recorded input and saves the frames it wants to look at. Each run steps at a fixed rate from a fixed seed, so after a change it can replay the same input and compare.</p></header>
  <ol class="loop reveal">${LOOP.map(([n, h, p, who]) => `<li class="${who}">${kicker(n)}<b>${h}</b><span>${p}</span></li>`).join("")}</ol>
  <div class="captures">${[[5, 180], [11, 360], [19, 600], [23, 720]].map(([i, f]) => `<figure><img class="pixel" src="${frame(i)}" width="480" height="640" alt="Strike Wing at frame ${f}" loading="lazy"><figcaption>frame_${String(f).padStart(5, "0")}.png</figcaption></figure>`).join("")}</div>
  <a class="arrow-link" href="${r("agents/")}">See the agent workflow <span aria-hidden="true">→</span></a>
</div></section>

<section class="replay wrap" aria-labelledby="replay-title">
  <header class="section-head reveal"><div>${kicker("F8 · recorded play")}<h2 id="replay-title">Express Yourself.</h2></div><p>“That part’s too hard.” Every play is recorded. Press F8 at the moment you mean, and your agent can replay it, read the game’s state and try a fix.</p></header>
  <div id="recorded-play" class="console" data-scrub data-initial="${MARK}" data-base="${r("img/scrub/")}" data-frames='${JSON.stringify(frames)}'>
    <div class="console-top">
      <div class="preview">
        <div class="plate scrub-screen"><img class="pixel" src="${frame(MARK)}" alt="Strike Wing at frame ${frames[MARK]}" width="480" height="640"><span class="replay-marker-leader" aria-hidden="true"></span></div>
        <p class="replay-marker-note kicker">F8 · m1 · tick <span data-frame>${frames[MARK]}</span></p>
      </div>
      <figure class="chat-window">
        <div class="chat-chrome">${kicker("Strike Wing · your agent")}<span class="illus">illustrative</span></div>
        <div class="chat-body"><p class="chat-you">I marked it with F8. Give me a little more room to dodge here.</p><p class="chat-agent">I’ll replay your marker, widen the gap between the bullets and show you frame 360 again.</p></div>
        <div class="chat-composer" aria-hidden="true"><span>Message your agent</span><span class="send">↑</span></div>
      </figure>
    </div>
    <div class="filmstrip">${strip}</div>
    <div class="track" style="--mark:${MARK / (frames.length - 1)}"><input type="range" min="0" max="${frames.length - 1}" value="${MARK}" step="1" aria-label="Recorded play frame" aria-valuetext="Frame ${frames[MARK]} of ${frames.at(-1)}"></div>
    <div class="controls">
      <div><button class="icon-btn" type="button" data-prev-frame aria-label="Previous frame"><i class="ph ph-skip-back" aria-hidden="true"></i></button><button class="icon-btn" type="button" data-play aria-pressed="false" aria-label="Play through the frames"><i class="ph ph-play" aria-hidden="true"></i></button><button class="icon-btn" type="button" data-next-frame aria-label="Next frame"><i class="ph ph-skip-forward" aria-hidden="true"></i></button><span class="readout" data-frame-readout>${frames[MARK]} / ${frames.at(-1)}</span><span class="visually-hidden" data-file>frame_00360.png</span></div>
      <button type="button" class="arrow-link marker-jump" data-jump-frame="${MARK}">Jump to F8 marker</button>
    </div>
    <p class="console-note">In ChatGPT and other apps that show MCP widgets, your agent can open this timeline right in the chat.</p>
  </div>
</section>

<section class="tools wrap" aria-labelledby="tools-title">
  <header class="section-head reveal"><div>${kicker("Plain files")}<h2 id="tools-title">Use the tools you’re used to.</h2></div><p>A Journeyman game is a folder of ordinary files. Draw maps in Tiled, animate in Aseprite, write code in your own editor. Your agent reads and edits the same files.</p></header>
  <ul class="tool-cards reveal">${TOOLS.map(([name, ext, text]) => `<li><b>${name}</b><code>${ext}</code><span>${text}</span></li>`).join("")}</ul>
  <div class="tool-plates">
    <figure class="editor-plate"><img src="${r("img/editor/hollow-grove-home.jpg")}" width="1495" height="785" alt="Hollow Grove open in the Journeyman editor" loading="lazy"><figcaption><b>The Journeyman editor, if you want it.</b> Select an object, copy its reference and paste it to your agent. When your agent edits the scene you have open, the editor merges its changes in. <a href="${r("editor/")}">The editor</a></figcaption></figure>
    <figure class="tiled-plate"><img class="pixel" src="${r("img/editor/tiled-grove.jpg")}" width="1536" height="1056" alt="Hollow Grove's grove.tmj map drawn by Tiled" loading="lazy"><figcaption class="credit">Hollow Grove’s <code>assets/maps/grove.tmj</code>, drawn by Tiled’s own renderer. The same file the engine loads and the editor paints.</figcaption></figure>
  </div>
</section>

<section class="get-going" aria-labelledby="go-title"><div class="wrap">
  <header>${kicker("Get going")}<h2 id="go-title">Paste one prompt, or run one command.</h2></header>
  <div class="go-grid">
    ${promptCard({ clip: false, note: "Already installed? Paste it anyway. It connects your agent." })}
    <div class="go-self">${installTabs("home")}</div>
  </div>
</div></section>
</div>`;
}
