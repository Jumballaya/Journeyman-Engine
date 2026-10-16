# Journeyman website design brief

## Messaging anchor

Journeyman is a small 2D engine for a human and their existing AI agent to make games in tandem. The partnership leads the website's story.

Headline:

> Work with your agent to build 2D games.

In one sentence: you work in tandem with your agent; it does the leg-work, and you oversee and steer it as it works.

The repository homepage also makes the agent's work concrete: “You describe the game. Your agent writes it as plain files, then builds, tests and plays it to check its work.” See `build.mjs`, homepage and agent workflow sections.

## What the design should communicate

- **A duo with distinct roles.** The human describes, oversees, judges, and steers. The agent edits, builds, tests, and plays to check its work.
- **A shared project.** Both work on the same plain files. A human's editor change becomes something the agent can see and continue.
- **An ongoing feedback loop.** You ask → the agent edits and checks → you play and judge → you steer again. Show the loop continuing, rather than ending with generated output.
- **Concrete shared evidence.** Actual games, editable project files, before/after frames, headless runs, and recorded plays make the partnership understandable. F8 markers let a person show the agent the moment they mean.
- **The agent the person already uses.** Claude, Codex, or another agent that can edit files, run commands, and read images. Journeyman is the engine and tooling; the website has no hosted AI chat service.

## Calls to action and language

Primary action: **Copy prompt for your agent**. Supporting action: **Install it yourself**. Explain that the person pastes the prompt into their agent to install Journeyman and make the first game.

Keep: work with your agent, in tandem, you + your agent, same game / same files, show it what you mean, play and steer.

Avoid replacing the positioning with generic “AI makes your game,” one-shot creation promises, or abstract inspiration slogans. Free, open source, and 2D are supporting facts; tandem game-making leads.

## Cobalt Manual: the design system

The site makes starting trivial (paste a prompt into your agent, or download), shows at once why working with an agent here is good, and shows the familiar tools.

Paper file "Journeyman Design", page **Cobalt Manual**: System, Home, Docs article, Docs index, Motion + navigation. Older designs and rejected directions were deleted in October 2026; don't revive them. Tokens are the `--color-v3-*` and `--font-v3-*` set; `site.css` `:root` mirrors them.

- **Color.** White page, ink `#0B0C10` text and 2px structural rules, ink-2 `#4A4D5A` secondary text, rule `#DCDEE6` hairlines, tint `#EEF0FF` for the prompt card and code. **Cobalt `#1E3CFF`** is the next action (copy prompt) and the stage the games sit on; hover `#1229D6`. **Signal yellow `#FFD12A`** is only the F8 marker, the playhead and the "you" steps on ink. Dark bands (ink) carry the agent loop and the footer.
- **Type.** Bricolage Grotesque 800 display (84/80 hero, 64–72 sections, 104 for "Express Yourself."), tight tracking −0.035em. Instrument Sans text 18/28. JetBrains Mono code 13–14. Silkscreen pixel labels (`.kicker`) at 11–12px only, never running text.
- **Shape.** Square corners. Hard offset shadows (4px ink on primary buttons, 8–10px on plates and cards) belong to actions and evidence, not to docs. Tilted stickers (±3°) name the roles on the hero stage: "You · Design it. Steer it." (white) and "Your agent · Build it. Check it." (ink), plus the yellow "F8 · you marked this".
- **Home order.** Hero (headline, product sentence, prompt card, "works with" chips; the cobalt stage with Strike Wing frame 360 ringed where F8 marked it, the stickers and the agent's terminal) → ten games → "Your agent can play." on ink (the four-step loop, four real headless frames) → "Express Yourself." (recorded-play console, chat, MCP timeline note) → "Use the tools you're used to." (five tool cards, editor and Tiled plates) → "Paste one prompt, or run one command." on cobalt (full prompt, install tabs on ink) → ink footer.
- **Get started** opens with two ways in: A · with your agent (recommended, cobalt offset) and B · yourself; the steps below are numbered.

Copy rules hold: no mirrored "You X. Your agent Y." sentences in running copy (the approved role stickers are the exception), no unverified claims, terminal and chat are labelled illustrative, "Express Yourself." stays. Aseprite support is the exported sprite sheet (PNG + JSON with tags); HTML/CSS is a supported subset; scripts are AssemblyScript, not TypeScript. Editor captures must not show local paths. The Tiled plate is Hollow Grove’s own `grove.tmj` rendered by Tiled’s `tmxrasterizer` (`/Applications/Tiled.app/Contents/MacOS/tmxrasterizer --scale 3 grove.tmj out.png`), not a borrowed screenshot.

### Docs, navigation and motion

The system carries readable docs, navigation, animation, findability and good UX. Paper boards: Docs article, Docs index, Motion + navigation.

- **Find.** ⌘K / Ctrl K or `/` opens search on every page. The docs index leads with a large search field and popular topics (`POPULAR` in `build.mjs`), then every guide and doc as bordered cards in groups. The docs sidebar starts with a search box.
- **Read.** Doc pages: breadcrumb (Docs / group), title, summary, and a meta row with the source file, Edit on GitHub and the page as raw Markdown for agents (`docs/<file>.md`, the repository file name, written beside the pages). Sidebar groups use Silkscreen labels; the current page has a cobalt square, not a bar or filled box. The outline marks the current section the same way and ends with Back to top. A 3px cobalt reading-progress bar sits under the nav. Previous/next are bordered cards.
- **Navigate.** Top bar: Get started, Games, Editor, Agent workflow, Docs, Download, search, Copy prompt (on the home page it appears once the hero's card has scrolled away). The phone menu lists every link and carries the copy button; docs on phones get a sticky "Browse documentation" toggle.
- **Motion.** `--dur-1` 120ms (hover, press), `--dur-2` 220ms (reveal, arrival), `--ease` cubic-bezier(.2,.7,.2,1). Primary buttons sink into their shadow; cards lift 2px with a cobalt shadow. On the home stage the stickers and terminal drop in one after another and the F8 ring pulses twice. Section heads fade up once. Never animate docs prose. `prefers-reduced-motion` turns all of it off.
