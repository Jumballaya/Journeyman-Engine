# Journeyman website design brief

## Messaging anchor

Journeyman is a small 2D engine for a human and their existing AI agent to make games in tandem. The partnership must lead the website's story, including ambitious visual revisions.

The user confirmed on 10 October 2026 that the original site's messaging was much closer to the intended positioning than the first design explorations. Preserve that meaning as the visual design evolves.

Original homepage headline:

> Work with your agent to build 2D games.

Original description preserved in the canonical Paper homepage:

> Work in tandem with your agent to build your games. Your agent does the leg-work and you oversee and steer it as it works.

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

## Editable Paper revisions

[Revisions page](https://app.paper.design/file/01M4M686VWKB06Z8BEMCPGJDHW/p-5-0) contains a messaging board and three visual concepts:

- **A · Playtest studio:** retains the original headline and makes the human-agent session visible beside a game capture.
- **B · Arcade editorial:** bold yellow/ink poster typography, with explicit “you + your agent” messaging and the ask/check/steer loop.
- **C · Scene notes:** editorial typography and the original site's concrete session example, followed by the human feedback step and shared editor files.

These are editable desktop design explorations. They do not replace the canonical 29-page Paper import or change the production website. Existing semantic theme tokens control light/dark surfaces and text; actual captures retain their original appearance. The arcade yellow/ink brand moment is intentionally consistent in either theme.

## Visual research

The following live product sites were visually studied on 10 October 2026. These observations guide presentation, not Journeyman's positioning.

- [Cursor](https://cursor.com/): substantial product proof. Adapt by showing the game and shared work at a useful scale.
- [Linear AI](https://linear.app/ai): agent activity and review are visible. Adapt by showing the entire feedback loop.
- [Lovable](https://lovable.dev/): a clear first action. Adapt with the existing copy-prompt action.
- [Replit](https://replit.com/): starter ideas make entry easier. Adapt with complete game projects.
- [Claude](https://claude.com/): human editorial typography. Adapt with approachable presentation of tandem work.
- [v0](https://v0.app/): real artifact galleries. Adapt with actual games rather than abstract AI imagery.

Existing Frontend Design and UI/UX Pro Max skills informed the visual and readability checks. Impeccable was installed for future design work; its context loader failed during this session, so the project context was read directly.

## Revision check

Before considering a revision ready, verify that a visitor can understand who makes the game, what each partner does, how both continue each other's work, and how to start with their own agent. Then review spacing, type, contrast, alignment, artboard fit, and visual variety in both themes.

## Faded print direction

The user supplied [turbopuffer](https://turbopuffer.com/), [TypeSafe](https://typesafe.ai/), [Vercel](https://vercel.com/), and [Mastra](https://mastra.ai/) as visual references and requested low contrast, geometric lines and forms, a washed-out 2000s indie album-cover aesthetic, and coffee-table art-book atmosphere.

**D · In tandem / Faded print + process** on the Revisions page explores that direction as an editable desktop homepage. It retains the original headline, tandem description, and copy-prompt action. Oversized cropped half circles, continuous vertical rules, and long diagonal pencil lines carry the geometry across the full page. Paired forms retain the human-agent motif.

Use uncoated paper, graphite text, dusty sage, pale slate, faded clay, and pencil rules. Type is Jost at 300–400 weight with IBM Plex Mono diagram labels. Keep generous negative space, asymmetric spreads, minimal outlines, and subdued game plates. Large title type can be delicate; useful copy and actions should remain legible. Avoid black/white extremes, bright accents, heavy cards, and glossy effects for this direction.

This example uses dedicated `--color-print-*` and `--font-print-*` tokens, leaving the existing shared theme presets unchanged. It is a light print concept with editable vector artwork and real captures styled at reduced opacity. The revised palette softens geometry to sage `#CCD2C8`, slate `#D0D2D9`, clay `#D8C9BE`, and pencil rules `#D8DBD1`; graphite text stays readable.

### Show the process and the feeling

The user loved the circles and overall direction, then requested more full-page geometry, lower contrast, and a visual account of the creative process. The revised hero places an illustrative human-agent conversation beside an actual Hollow Grove editor capture: describe a feeling, make something playable, and shape it together. The conversation is example copy, not a transcript or a claim of a hosted chat feature.

A real Strike Wing capture leads into a recorded-play view with seven actual frames, a timeline scrubber, and illustrative F8 marker feedback. The person judges the game and points to a moment; the agent can replay, change, and test again. “One more try” and “Little by little, it becomes yours” express playful iteration while preserving the original tandem positioning. Keep this loop visible in later revisions instead of reducing it to abstract geometry or a finished-game gallery.

### E refinement

**E · In tandem / Refined print** is a copy of D refined after independent design and UX/evidence critiques. The user excluded contrast from the critique and asked to preserve the direction. E keeps the print palette and typography, groups the copy-prompt action with its explanation, clarifies pasting into an existing agent, replaces the abstract cube with a flat shared-map-file diagram, anchors feedback leaders in real captures, varies the two recorded-play moments, aligns the filmstrip and marker, and reduces repeated captions. “One more try” now aligns with the continuing feedback exchange. D remains available for comparison.

### Copy and partnership revision

The user flagged eight screenshots, rejected vague copy including “the map you both shape,” and asked to make the partnership unmistakable. Corey Haines’s `copywriting` and `copy-editing` skills were researched, read, and installed in both `~/.codex/skills` and `~/.claude/skills`. Claude Code 2.1.296 generated two rounds of candidates using these skills. The raw drafts and selection notes live in `~/.cache/journeyman-paper/`. Product claims were checked against `site/build.mjs`, `docs/agents.md`, and `docs/plays.md`; invented enemy behavior and automatic successful checks from the drafts were excluded from final copy.

E preserves the original headline and tandem lead. It now places the paired circles beside the opening action, shows a four-turn illustrative exchange beside the actual Hollow Grove editor, and explains the human's saved change becoming the agent's next starting point. The green chat fill was removed. Shorter replies, a larger human request, role labels, and narrow sage/slate rules distinguish the turns. A flat map diagram names concrete contributions and uses the factual caption “The file your agent edits.” The agent-play plate uses the circular composition directly on the page. The recorded-play section explains F8 and replaying the marked moment, with future-tense example feedback rather than claims that a fix already happened.

The user edited the Venn positions and changed its center to “Your Game.” Preserve those edits. A separate editable **Journeyman · Copy desk / Claude shortlist** artboard below E contains alternatives for the side descriptions; they have not been applied over the user's edits.

The user subsequently rejected the quotation-style conversation and supplied three screenshots of their ChatGPT/Codex desktop app as a layout reference. E's conversation is now a simplified desktop chat mockup: minimal window controls and title, right-aligned rounded user bubbles, plain left-aligned agent replies, and an inset rounded composer. The project and existing exchange remain; sidebars, activity feeds, model menus, timestamps, edit reports, and extra controls are omitted. Use the existing Geist body token at 18px/26px within the mockup and preserve Jost for the surrounding print design. This is an editable illustration, not a capture of a completed session. The Venn and retained folded-map mark are unchanged.

A matching pencil leader now connects the “Connect the ponds with a path” user message to the pond in the actual editor capture. It complements the existing human-editor clearing annotation: the person can ask their agent to change the game or make an edit directly. Both leaders are editable vector overlays, not alterations to the actual capture.

The user rejected both the outline icon set and the later geometric emblem set, and explicitly asked to delete them. Both Paper icon artboards were deleted. Only the folded map from the first set was retained, moved into E's navigation, and resized as the Journeyman mark. Do not regenerate icon families or revive the rejected boards unless asked.

For this revision the user explicitly excluded contrast criticism and requested that the muted print direction be preserved. Review spacing, hierarchy, alignment, fit, repetition, product truth, and natural wording without changing the agreed palette.

### F avant-garde print exploration

The user requested an exact copy of the latest E, then a full avant-garde revision. **F · In tandem / Avant-garde** sits immediately to the right of E on the Revisions page. E, including the user's recent Venn and copy edits, remains intact.

F turns the homepage into an experimental print spread: a three-line 144px original headline, oversized circles cropped at the page edge, vertical captions, continuous pencil rules and sweeping curves, a staggered chat/editor collage, monumental “by hand” typography beside a tilted shared-file diagram, and an angled real Strike Wing plate. The replay becomes a full-width sequence of seven actual frames with a centred marker and concrete human/agent feedback. A 440px tilted “in tandem” closes the composition. Practical copy and actions stay upright while display typography and plates carry the experiment.

F retains the fixed print tokens, original folded-map mark, existing-agent copy-prompt action, real editor and game captures, illustrative chat, saved-file collaboration, and F8 replay workflow. The chat request and human edit have separate vector leaders; both were reanchored after the windows moved. The canonical pages and shared light/dark aliases are unchanged. This is an editable desktop design exploration, with no production-site implementation or runtime interaction added.

Each major spread was reviewed in Paper for typography, spacing, alignment, fit, repetition, and product meaning. Intentional giant forms and display crops remain; accidental label clipping and overlaps were corrected. JSX and PNG backups are stored in `~/.cache/journeyman-paper/`.

### F kinetic revision and pinned spreads

The user pinned the original F cover, the “Show it what you mean” replay spread, and the monumental “in tandem” closing. The replay now follows the cover immediately; the shared-file and agent-play spreads follow it. Preserve those three anchor compositions. The user rejected angled product views, so the Venn, chat/editor pair, shared-file diagram, and game plate are upright. The approved Venn wording is “Design it. / Steer it.” for the person, “Your / Game” at the overlap, and “Build it. / Check it.” for the agent. Side groups sit outward from the overlap with the horizontal rule between their two lines; do not crowd them toward the center.

F now contains a native animated Spiral shader at 8% speed behind the opening's upper-right geometry. Thin strokes, transparent backing, 35% opacity, and luminosity blending make its motion read as faint pencil rings in the existing palette. This is real Paper shader animation and can export to video. General scroll-driven transitions, animated chat turns, and playable scrubber interactions would require a coded prototype. E and the user's saved F copy remain available for comparison; the production site is unchanged.

The user found the shared-file spread's oversized “by hand” distracting from its meaning, then rejected “Make an edit. Your agent builds on it.” as formulaic AI copy. The approved message is now presented as a complete, equally weighted heading: “Some changes are easier by hand.” Supporting copy explains that the editor saves to the same map files the agent works on, with painting a clearing and asking for an enemy as the example. A full-width editable diagram shows the human's edit, the saved `grove.tmj` map, and the agent's next build in left-to-right order. Arrows point through the shared file toward the next build, replacing the previous two inward arrows. The circle/map artwork is substantially larger, and the agent's example matches the clearing/enemy copy. The print palette and surrounding anchor spreads are preserved.

Copy constraint: avoid paired headline fragments and the recurring “Do X. Your agent does Y.” construction. Use ordinary sentences that explain the concrete interaction. Preserve the Venn wording the user explicitly selected rather than applying this constraint over their edits.

The user flagged repeated circles in the agent-play section after the shared-file spread. That section now uses an open rectangular frame, small registration strokes, offset vertical guides, and extended horizontal rules around the real Strike Wing capture. Its orbit and looping curve were replaced. The faded clay background is contained within the preceding shared-file section, so it no longer spills behind the game plate. The game image, copy, and print tokens remain intact. Keep geometric variety between adjacent spreads rather than reusing a circle at the right of each one.

### F content sequence and familiar tools

The user subsequently removed the entire “Some changes are easier by hand” spread. That section, its file diagram, and its clay background have been deleted from the current F. The earlier descriptions above document its history, not the current approved content. Do not restore it without a new request.

F now follows four main messages: “Work with your agent to build 2D games,” “Show it what you mean,” “Your agent can play,” and “Use the tools you’re used to,” followed by the pinned “in tandem” closing. The opening, replay, and closing compositions remain intact. The page fits its content at 1440 × 7469.

The familiar-tools spread features an upright official Tiled editor-window image, an editable JSON excerpt from Hollow Grove’s `map_grove.prefab.json`, and actual HTML/CSS and AssemblyScript excerpts from `area.ui.html` and `hero.ts`. Supporting copy identifies Tiled `.tmj` maps and `.tsj` tilesets, JSON scenes and prefabs, HTML/CSS screens, and AssemblyScript gameplay. These formats were verified against `docs/content.md`, `docs/editor.md`, and the demo files. The Tiled image is sourced from https://www.mapeditor.org/img/screenshot-front.png and visibly credited to mapeditor.org; it depicts Tiled’s Super Catboy example, not a Journeyman game. All code excerpts remain editable text. Thin straight rules and open brackets add structure without another right-side circle. Existing print tokens are unchanged.

The complete F and new spread were visually reviewed for spacing, type hierarchy, lane alignment, fit, and variety. The agreed muted palette was preserved. Current PNG and exact JSX backups are stored in `~/.cache/journeyman-paper/revision-F-v3-tools.*`; the production site is unchanged.

### Consistent desktop chat in the replay spread

The user asked for the replay feedback to use the opening’s chat design. The separate “YOU / AT YOUR MARKER” and “YOUR AGENT” quotations have been replaced with an editable clone of the opening’s ChatGPT desktop window. It retains the same minimal window chrome, Geist typography, graphite user bubble, plain agent reply, and composer, and contains the existing Strike Wing feedback exchange. The window is wider to suit the spread. “one more try” and “PLAY THE NEXT VERSION” sit beside it; the real frame strip, timeline, F8 marker, and replay preview are unchanged. The spread gained 96px of breathing room, and the complete F fits its content at 1440 × 7565. The existing opening chat is unchanged.

The user then requested a complete, unclipped closing circle and a slightly smaller gap before the familiar-tools heading. The closing clay disc and its pencil outline now fit entirely inside the spread, with the existing typography preserved. The agent-play spread is 96px shorter, with its bottom rules adjusted to keep the caption clear. F now fits its content at 1440 × 7469.

The user subsequently requested removal of the “in tandem” closing, explicitly confirming that section rather than the familiar-tools spread. The entire closing, including its display typography, circle, closing copy-prompt action, and colophon, has been deleted from current F. The footer now follows the tools section. The opening copy-prompt action remains. F fits its content at 1440 × 6209. Earlier instructions pinning the closing are superseded by this removal; do not restore it without a new request.

### Completed F homepage

The user requested the footer as the final homepage addition. F now ends with a complete, editable footer in the print palette: the retained folded-map mark and wordmark, the original small-engine description, Learn/Make/Project navigation matching `site/build.mjs`, the Patrick Burris credit, and the free/open-source/MIT license line. The earlier thin footer was replaced. Fixed-width navigation columns keep the links aligned; pencil rules and generous spacing provide a quiet ending without restoring the removed closing spread. The full desktop homepage fits its content at 1440 × 6517 and is named “F · Homepage / Finished print” in Paper. The final design snapshot and exact JSX are saved as `~/.cache/journeyman-paper/homepage-F-final.*`. This completes the Paper desktop homepage design; it does not implement or publish the production site.


### Documentation research and the second non-home pass

The user rejected the initial non-home pass as flat and hard to read, and specifically rejected the filled green sidebar state and its narrow vertical edge. The references were then studied against [Mintlify navigation](https://www.mintlify.com/docs/organize/navigation), [Vercel CLI documentation](https://vercel.com/docs/cli), and [Stripe API documentation](https://docs.stripe.com/api). The lessons were separation of global navigation, chapter navigation and local outline; a calm reading measure; clearer heading weight; and code controls outside the example. These are layout observations, not a change to product claims.

All ten editable Paper reference boards now have a full-width chapter opening above a 216px sidebar, 712px reading column and 224px outline, with 72px gutters. Navigation is grouped into Guides, Make a game, Build & play and Reference. The current page uses an underline, without a filled box or vertical bar. Jost carries the chapter title; Geist 18px/31px carries prose; Geist Mono 14px/24px carries code in quiet slate-sage panels with a separate utility header. Tables have an explicit header surface and consistent column lanes. Decorative geometry has been removed from behind reading text.

The documentation index is a grouped, full-width list with separate title and description lanes. Guides have readable Geist prose and code. Downloads group CLI and Editor by platform, with platform names before filenames, then engine/support files and distinct first-launch and source-build sections. Game detail pages use a full-width title, larger screenshots and a separate facts column; the gallery has two substantial columns. Editor features pair larger product captures with appropriately scaled headings. These revisions keep the approved palette and footer while giving each page a structure suited to its content.

### Production implementation

The canonical Paper F homepage and all 28 other pages are implemented in the existing static builder. `home.mjs` follows the four approved spreads; `home.css` preserves their desktop coordinates and provides a reflowed mobile edition. The replay uses the real captured frames with thumbnail selection, keyboard-operable range input, previous/next, playback and an F8 marker jump. Chat windows remain clearly identified illustrative exchanges. The faint moving pencil rings are a CSS interpretation of Paper's animated spiral and respect reduced motion.

The print theme is a single shared palette; the previous system-theme switch has been removed. Light editor captures are used where available. Source Markdown remains intact, including the CLI-generated help. Search, copying, OS tabs, download selection and screenshot viewing remain functional. Reference navigation collapses on mobile, and wide code and tables scroll within their reading column.

Validation: all 29 pages build, 172 search entries resolve, generated local links/assets/anchors and unique headings pass, and the install-guide regression test passes. Browser checks at desktop and 390px phone width covered all routes, with no horizontal page overflow. Copying, search, mobile navigation, OS selection, release download selection, screenshot viewing and replay selection/playback were exercised. The local browser pass found and fixed a homepage class-name collision affecting the opening action.
