// The approved Paper F spread, translated into semantic HTML and a responsive print layout.
// Product windows are illustrative exchanges; replay frames are from the real demo run.
const svg = (viewBox, body, cls = "") => `<svg class="${cls}" viewBox="${viewBox}" fill="none" aria-hidden="true">${body}</svg>`;
const chat = (project, turns, cls = "") => `<div class="desktop-chat ${cls}" aria-label="Example conversation about ${project}">
  <div class="chat-chrome"><span class="window-dots" aria-hidden="true"><i></i><i></i><i></i></span><span>ChatGPT</span></div>
  <div class="chat-body"><p class="chat-project">${project}</p>${turns.map(([role, text]) => `<p class="chat-${role}">${text}</p>`).join("")}</div>
  <div class="chat-composer"><span>Message ChatGPT</span><span class="send-mark" aria-hidden="true">↑</span></div>
</div>`;

export function homePage({ r, frames, agentButton }) {
  const frame = (i) => r(`img/scrub/f${String(i).padStart(2, "0")}.jpg`);
  const strip = [0, 7, 9, 11, 15, 19, 23].map((i) => `<button type="button" data-frame-index="${i}" aria-label="Show frame ${frames[i]}"${i === 11 ? ' aria-current="true"' : ""}><img class="pixel" src="${frame(i)}" width="480" height="640" alt="Strike Wing at frame ${frames[i]}" loading="lazy"></button>`).join("");
  const venn = svg("0 0 500 300", `<circle cx="169" cy="138" r="119" fill="var(--sage)"/><circle cx="331" cy="138" r="119" fill="var(--slate)"/><path d="M250 51 A119 119 0 0 1 250 225 A119 119 0 0 1 250 51Z" fill="var(--clay)"/><g stroke="var(--line)" stroke-width="1"><circle cx="169" cy="138" r="132"/><circle cx="331" cy="138" r="132"/><path d="M0 138H500 M250 0V278"/></g>`, "venn-art");
  return `<div class="print-home">
${svg("0 0 1440 6050", `<g stroke="var(--line)" opacity=".52"><path d="M72 0V6050 M504 0V6050 M936 0V6050 M1368 0V6050 M-300 550L1750 7100 M-254 550L1796 7100 M-208 550L1842 7100 M-140 1850C1260 700 1630 2460 750 3000S-500 3500 700 4740S1920 6250 -100 6850"/></g>`, "page-linework")}
<div class="cover-disc" aria-hidden="true"></div><div class="print-rings" aria-hidden="true"><i></i><i></i><i></i><i></i></div>
<section class="print-cover" aria-labelledby="home-title">
  <h1 id="home-title">Work with<br>your agent<br>to build 2D games.</h1>
  <div class="partnership">
    <div class="venn" role="img" aria-label="You design and steer. Your agent builds and checks. Together, your game.">${venn}<div class="venn-human"><span>YOU</span><p>Design it.<br>Steer it.</p></div><div class="venn-agent"><span>YOUR AGENT</span><p>Build it.<br>Check it.</p></div><p class="venn-game">Your<br>Game</p></div>
    <div class="opening-action"><p>Your agent does the leg-work.<br>You play each build and decide<br>what changes next.</p>${agentButton("print-action")}<p class="action-note">Paste it into Claude Code, Codex or another agent that runs commands.<br>It installs Journeyman and the editor, then makes your first game.</p></div>
  </div>
  <div class="shared-workspace">
    ${chat("Hollow Grove", [["you", "Connect the ponds with a path."], ["agent", "I’ll leave room to walk between the trees."], ["you", "I painted a clearing. Put an enemy here."], ["agent", "I’ll add the enemy to the clearing you painted."]], "grove-chat")}
    <figure class="grove-editor"><figcaption><span>Journeyman editor / Hollow Grove</span><span class="measure">the same project</span></figcaption><img src="${r("img/editor/hollow-grove-home.jpg")}" alt="Hollow Grove open in the Journeyman editor with its tile painting tools" width="1495" height="785" loading="lazy"><p><span class="measure">YOU / EDITOR</span> Paint the clearing by hand</p></figure>
    ${svg("0 0 1296 620", `<g stroke="var(--fg)" stroke-width="1.25"><path d="M439 270H480V194H792"/><circle cx="802" cy="194" r="10"/><path d="M920 536H1070V260H977"/><circle cx="967" cy="260" r="10"/></g>`, "workspace-leaders")}
  </div>
  <div class="cover-caption"><p>Example exchange beside the actual Hollow Grove editor.</p><div><span>Free · Open source</span><a href="${r("start/")}">Install it yourself ↗</a></div></div>
</section>

<section class="print-replay" aria-labelledby="replay-title">
  <div class="replay-disc" aria-hidden="true"></div>
  <h2 id="replay-title">Show it what<br>you mean.</h2>
  <p class="replay-quote">“That part’s too hard.”<br>Show your agent which part.</p>
  <div id="recorded-play" class="replay-work" data-scrub data-initial="11" data-base="${r("img/scrub/")}" data-frames='${JSON.stringify(frames)}'>
    <p class="replay-explanation">Your play is recorded. Press F8 to mark a moment, then find it on the timeline. Your agent can replay it and inspect what happened.</p>
    <div class="replay-heading"><span>Strike Wing / your recorded play</span><span>Example feedback</span></div>
    <div class="replay-preview"><div class="scrub-screen"><img class="pixel" src="${frame(11)}" alt="Strike Wing at frame 360" width="480" height="640" loading="lazy">${svg("0 0 336 448", '<g stroke="var(--fg)" stroke-width="1.25"><path d="M298 147H400V300H820"/><circle cx="280" cy="147" r="18"/></g>', "replay-marker-leader")}</div><div class="replay-marker-note"><span class="measure">F8 / YOUR MARKER</span><p>More room to dodge<br>around the ship.</p></div><span class="replay-tick measure">tick <span data-frame>360</span><br>of 720</span></div>
    <div class="replay-filmstrip">${strip}</div>
    <div class="replay-track"><input type="range" min="0" max="${frames.length - 1}" value="11" step="1" aria-label="Recorded play frame" aria-valuetext="Frame 360 of 720"><span class="f8-mark" aria-hidden="true"></span><span class="scrub-playhead" aria-hidden="true"></span></div>
    <div class="replay-controls"><div><button class="icon-btn" type="button" data-prev-frame aria-label="Previous frame"><i class="ph ph-skip-back" aria-hidden="true"></i></button><button class="icon-btn" type="button" data-play aria-pressed="false" aria-label="Play through the frames"><i class="ph ph-play" aria-hidden="true"></i></button><button class="icon-btn" type="button" data-next-frame aria-label="Next frame"><i class="ph ph-skip-forward" aria-hidden="true"></i></button><span class="measure" data-frame-readout>360 / 720</span><span class="visually-hidden" data-file>frame_00360.png</span></div><button type="button" class="measure marker-jump" data-jump-frame="11">F8 MARKER / SCRUB TO THIS MOMENT</button></div>
  </div>
  ${chat("Strike Wing", [["you", "Give me a little more room to dodge here."], ["agent", "I’ll replay your mark and try a wider gap."]], "replay-chat")}
  <div class="one-more"><p>one more try.</p><span class="measure">PLAY THE NEXT VERSION</span></div>
</section>

<section class="print-agent" aria-labelledby="agent-title">
  <div class="agent-copy-content"><h2 id="agent-title">Your agent<br>can play.</h2><p>Your agent can build, test and play the game to check its work. You play each version and decide whether it’s fun.</p><a href="${r("agents/")}">See the agent workflow ↗</a></div>
  <figure class="agent-plate"><span class="measure vertical-caption">BUILD / TEST / PLAY</span><img class="pixel" src="${frame(19)}" alt="Strike Wing's plane flying between bullets and clouds at frame 600" width="480" height="640" loading="lazy">${svg("0 0 800 760", `<g stroke="var(--fg)" opacity=".38"><path d="M170 128V30H280 M540 30H630V124 M630 548V630H536 M286 630H170V532"/></g><g stroke="var(--line)"><path d="M670 0V706 M704 168V760 M40 268H800 M0 708H734 M56 232H800 M40 656H120"/></g>`)}<figcaption>Strike Wing, made with Journeyman.</figcaption></figure>
  ${svg("0 0 1440 240", `<path d="M-80 62H1440 M448 108H1504 M612 0V240 M1440 202H820V154" stroke="var(--line)"/>`, "agent-bottom-lines")}
</section>

<section class="print-tools" aria-labelledby="tools-title">
  <h2 id="tools-title">Use the tools<br>you’re used to.</h2><p class="tools-intro">We use Tiled for maps and JSON for scenes and prefabs. Screens are HTML/CSS, and gameplay is AssemblyScript.</p>
  <figure class="tiled-plate"><img src="${r("img/editor/tiled.png")}" alt="Tiled's map editor showing its Super Catboy example" width="1527" height="599" loading="lazy"><figcaption><strong>Tiled</strong><span class="measure">.tmj maps · .tsj tilesets</span><a href="https://www.mapeditor.org/">Editor image: mapeditor.org</a></figcaption></figure>
  <div class="json-sample no-copy"><h3>JSON</h3><p class="measure">map_grove.prefab.json · excerpt</p><pre><code>{
  "components": {
    "TileMapComponent": {
      "map":
        "assets/maps/grove.tmj"
    }
  }
}</code></pre></div>
  <div class="html-sample no-copy"><h3>HTML / CSS</h3><p class="measure">assets/ui/area.ui.html · excerpt</p><pre><code>#hud {
  display: flex;
  height: 48px;
}</code></pre></div>
  <div class="script-sample no-copy"><h3>AssemblyScript</h3><p class="measure">assets/scripts/hero.ts · excerpt</p><pre><code>const SPEED: f32 = 80;
const SWING_SECONDS: f32 = 0.22;
const HURT_SECONDS: f32 = 1.0;</code></pre></div>
  ${svg("0 0 1440 1320", `<path d="M0 344H1440 M48 364H100V328 M1340 328V364H1392 M72 1032H1368 M736 1032V1320 M0 1290H1440" stroke="var(--line)"/>`, "tools-lines")}
</section>
</div>`;
}
