const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle, plain } = require('./studio-host.cjs');

const games = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: ['scenes/level.scene.json'] }] } };
const now = Math.floor(Date.now() / 1000);
const editing = (notes) => ({ structuredContent: { editors: [{ pid: '42', scene: 'scenes/level.scene.json', selection: ['Player'], unsaved: [], notes }] } });
const level = { structuredContent: { scene: 'scenes/level.scene.json', entities: [
  { where: 'entities[0]', name: 'Player', prefab: 'assets/prefabs/ship.prefab.json' },
  { where: 'entities[0]/Shadow', name: 'Shadow', inherited: true },
  { where: 'entities[1]', name: 'Door' }] } };

test('the Editor tab shows what the person has open and pointed out; a note goes to the composer', async () => {
  const s = await studio(games, { answers: { editor_state: editing([{ id: 1, at: now, scene: 'scenes/level.scene.json', entities: ['Player'], text: 'jumps too low' }]),
    scene_view: level }, widgetState: { route: '/editor' } });
  await settle(6);
  assert.equal(s.current(), 'Editor');
  assert.match(s.app.text, /scenes\/level\.scene\.json/);
  assert.match(s.app.text, /Selected:  Player/);
  assert.match(s.app.text, /jumps too low Player in scenes\/level\.scene\.json/);
  s.find((e) => e.attributes['data-key'] === 'tree:42:1')[0].fire('click');
  await settle();
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').at(-1).params.content[0].text, 'About Player in scenes/level.scene.json: jumps too low');
  assert.deepEqual(s.contexts().at(-1).structuredContent.editor.selection, ['Player']);
});

test('an entity picked is shown in the editor; a prefab\'s own children aren\'t offered', async () => {
  const s = await studio(games, { answers: { editor_state: editing([]), scene_view: level }, widgetState: { route: '/editor' } });
  await settle(6);
  assert.equal(s.find((e) => e.attributes['data-key'] === 'tree:entities[0]/Shadow').length, 0);
  s.find((e) => e.attributes['data-key'] === 'tree:entities[1]')[0].fire('click');
  await settle(6);
  assert.deepEqual(s.calls().filter((c) => c.name === 'editor_show'), [{ name: 'editor_show', arguments: { scene: 'scenes/level.scene.json', entity: 'Door', game: '/g/pip' } }]);
  // The tab's game, not whichever the server has open (another chat may have opened another).
  for (const name of ['editor_state', 'scene_view']) { const calls = s.calls().filter((c) => c.name === name); assert.ok(calls.length && calls.every((c) => c.arguments.game === '/g/pip'), name); }
  assert.match(s.app.text, /Door is shown in the editor/);
});

test('with no editor open, it offers to open one', async () => {
  const s = await studio(games, { answers: { editor_state: { structuredContent: { editors: [] } }, scene_view: level }, widgetState: { route: '/editor' } });
  await settle(6);
  assert.match(s.app.text, /not open/);
  s.button('Open in editor').fire('click');
  await settle(6);
  assert.ok(s.calls().some((c) => c.name === 'project_open_editor'));
});

test('a note pointed out while the Studio is open is drafted in the chat, on any tab', async () => {
  let notes = [{ id: 1, at: now - 600, scene: 'scenes/level.scene.json', entities: [], text: 'old' }];
  const s = await studio(games, { answers: { editor_state: () => editing(notes) }, caps: { experimental: { 'openai/message': {} } } });
  await settle(6);
  assert.equal(s.current(), 'Overview');
  notes = [...notes, { id: 2, at: now, scene: 'scenes/level.scene.json', entities: ['Door'], text: 'should be locked' }];
  await new Promise((resolve) => setTimeout(resolve, 2100));
  await settle(6);
  const drafts = s.sent.filter((m) => m.method === 'ui/message');
  assert.equal(drafts.length, 1, 'the note made before the Studio opened stays in the editor');
  assert.equal(drafts[0].params.content[0].text, 'About Door in scenes/level.scene.json: should be locked');
  assert.deepEqual(plain(drafts[0].params._meta['openai/message']), { target: 'active', send: false });
  const kept = notes;
  notes = [];  // the editor gone a moment, then back with the same notes
  await new Promise((resolve) => setTimeout(resolve, 2100));
  notes = kept;
  await new Promise((resolve) => setTimeout(resolve, 2100));
  await settle(6);
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').length, 1, 'drafted once');
});

test('Render shows the scene as the engine draws it', async () => {
  const s = await studio(games, { answers: { editor_state: editing([]), scene_view: level,
    scene_render: { content: [{ type: 'text', text: 'drawn' }], _meta: { 'jm/image': 'data:image/jpeg;base64,abc' } } }, widgetState: { route: '/editor' } });
  await settle(6);
  s.button('Render').fire('click');
  await settle(6);
  assert.deepEqual(s.calls().filter((c) => c.name === 'scene_render'), [{ name: 'scene_render', arguments: { scene: 'scenes/level.scene.json', game: '/g/pip' } }]);
  assert.equal(s.find((e) => e.tag === 'img')[0].attributes.src, 'data:image/jpeg;base64,abc');
});

test('another game\'s editor, scene and picture never show under this one, however late they answer', async () => {
  const two = (open) => ({ structuredContent: { open, games: [
    { name: 'Alpha', path: '/g/a', opened: 1, scenes: ['scenes/level.scene.json'] },
    { name: 'Beta', path: '/g/b', opened: 1, scenes: ['scenes/level.scene.json'] }] } });
  let open = '/g/a';
  const held = [];
  const late = (value) => () => (open === '/g/a' ? new Promise((resolve) => held.push(() => resolve(value))) : value);
  const of = (who) => ({
    editor: { structuredContent: { editors: [{ pid: who, scene: 'scenes/level.scene.json', selection: [who + ' Player'], unsaved: [], notes: [] }] } },
    view: { structuredContent: { scene: 'scenes/level.scene.json', entities: [{ where: 'entities[0]', name: who + ' Door' }] } },
    render: { content: [], _meta: { 'jm/image': 'data:image/jpeg;base64,' + who } },
  });
  const a = of('Alpha'), b = of('Beta');
  const s = await studio(two(open), { widgetState: { route: '/editor' }, answers: {
    games: () => two(open),
    open_game: (args) => { open = args.game; return { content: [{ type: 'text', text: 'ok' }] }; },
    editor_state: () => (open === '/g/a' ? late(a.editor)() : b.editor),
    scene_view: () => (open === '/g/a' ? late(a.view)() : b.view),
    scene_render: () => (open === '/g/a' ? late(a.render)() : b.render) } });
  await settle(6);
  s.button('Render').fire('click');
  await settle(3);
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/editor?game=/g/b' } });
  await settle(10);
  for (const release of held.splice(0)) release();
  await settle(10);
  const clean = (when) => {
    assert.doesNotMatch(s.app.text, /Alpha (Player|Door)/, when);
    assert.ok(!s.find((e) => e.tag === 'img').some((img) => /Alpha/.test(img.attributes.src)), when);
    assert.doesNotMatch(JSON.stringify(s.contexts().at(-1)), /Alpha/, when);
  };
  clean('as Alpha\'s late answers land');
  await new Promise((resolve) => setTimeout(resolve, 2100));  // the next poll: Beta's
  await settle(10);
  clean('after');
  assert.match(s.app.text, /Beta Door/);
  assert.match(s.app.text, /Beta Player/);
});
