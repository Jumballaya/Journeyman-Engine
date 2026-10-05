# @demos/common

Script code shared by the demos: a project lists it in `.jm.json` as
`"scriptLibraries": { "@demos/common": "../common" }`, and `jm build` / `jm test`
copy it into `assets/scripts/node_modules/@demos/common`.

- `Dialog`: a box that types out lines over a paused world, optionally ending
  in a choice (used by the dungeon and the JRPG).
