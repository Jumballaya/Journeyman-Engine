# Gaps found making Ash and Iron

Made entirely through the editor; what got in the way, and what became of it.

| Gap | Resolution |
|---|---|
| No mouse input for games | Engine: mouse buttons are bindable keys, `Input.pointer`, `pointerInside`, `wheel`; the editor's Game view forwards the mouse |
| A failed build emptied `build/`, killing the preview | Builds assemble in `build.next/` and swap in only when complete |
| Script compile errors didn't say where | The Console attaches file and line; double-click opens it |
| Data tables were text-only and slow to fill | Typed columns, Tab-through cells (Tab out of the last adds a row), several tables per file, sprite/sound/record-reference cells with pickers |
| New Scene made an unsaved "untitled" | It asks for a name and creates the file, like every other New |
| No way to choose the starting scene | Scene Inspector: Start the Game Here |
| The Scene view's floating toolbar and tile palette ignored clicks | The view under them no longer claims the mouse |
| Asset pickers listed matches in project order | Name matches rank first; Enter takes the top one |
| Tabs of moved files pointed at the old path | Open tabs follow their files |
| Prefabs were added to the manifest's scene list | Only scenes are |
| The Console's level chips hid a level when clicked | They show only that level (again for all) |
