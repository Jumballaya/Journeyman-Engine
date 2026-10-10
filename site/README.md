# The Journeyman website

The site published to GitHub Pages: home, Get started, Agent workflow, the games, the editor,
downloads, and the docs, which are rendered from the repo's `docs/*.md` on every build.

```sh
cd site
npm install
npm run build          # writes dist/
npm run serve          # builds, then serves dist/ at http://127.0.0.1:4173
```

`.github/workflows/pages.yml` builds and deploys it on every push to `master` that touches
`site/` or `docs/`. In the repo's settings, Pages must use **GitHub Actions** as its source.

## Layout

- `build.mjs`: every page's content and layout, the game and docs data, markdown rendering
  and the search index. Links are relative, so `dist/` works at any base path.
  `404.html` works out its base from its own URL (Pages serves it at whatever path was missed).
- `src/css/site.css`: the design system. Its colors are the editor's palettes
  (`editor/Theme.cpp`), so the site and the editor screenshots match in both themes.
- `src/js/site.js`: search, theme toggle, copy buttons, tabs, the frame scrubber, the lightbox
  and the download picker.
- `src/img/`: real captures. `games/` and `scrub/` come from headless engine runs
  (`JM_CAPTURE_*`, see docs/testing.md); `scrub/replay.txt` is the replay behind the home page
  scrubber. `editor/` holds editor captures in pairs: `name.jpg` (dark) and `name-light.jpg`.

## Editor screenshots

An editor shot with a `-light` twin is rendered as both images in one box, and the page's theme
picks which one shows, so switching themes swaps the picture in place. The two must frame
identically, so they are captured with the same scripted steps and cropped with the same boxes:

```sh
site/tools/capture-editor.sh dark      # scene, tiles, data, ui, play
site/tools/capture-editor.sh light
site/tools/capture-play-pair.sh        # play again: one run, paused, shot in both themes
W="${JM_CAPTURE_WORK:-${TMPDIR:-/tmp}/jm-site-capture}"
python3 site/tools/prep-editor.py "$W/out/ed2" dark
python3 site/tools/prep-editor.py "$W/out/ed2" light --light
```

The scripts work on scratch copies of the demos, need a release build of the editor and `jm`,
and back up and restore the editor's settings folder around every run. The play shot runs on
real time, so separate runs drift; `capture-play-pair.sh` pauses one run and switches the
appearance between its two shots instead.

## Multiplayer screenshots

Build the current CLI, engine and dedicated server with `scripts/build-release.sh`,
then install the script dependencies in each multiplayer demo's `assets/scripts/`.
Run `python3 site/tools/capture-multiplayer.py` on a machine with a display and Pillow.
It builds all three games, captures their title screens and plays the multiplayer
test replays: three peers for Pellet Party, a server and two clients for Tank Arena,
and a matchmaker followed by a peer-to-peer Checkers match. It checks the connected
peers' scenes and shared entities before exporting the site JPEGs and 640px variants.
Raw frames, state dumps and logs stay in `build/site-multiplayer/`.
