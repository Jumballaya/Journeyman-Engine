const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle } = require('./studio-host.cjs');

const now = Math.floor(Date.now() / 1000);
const listing = (open) => ({
  content: [{ type: 'text', text: 'Games' }],
  structuredContent: { open, games: [
    { name: 'Neon Vow', path: '/g/neon', opened: now - 60, scenes: ['scenes/main.scene.json', 'scenes/levels/one.scene.json'] },
    { name: 'Tetris', path: '/g/tetris', opened: 0, scenes: ['scenes/a.scene.json'] },
    { name: 'Gone', path: '/g/gone', opened: 5, scenes: [], missing: true }] },
  _meta: { 'jm/thumbs': { '/g/neon': 'data:image/jpeg;base64,neon' } },
});
const plays = { structuredContent: { plays: [
  { id: '20261015-101500', started: '2026-10-15T10:15:00Z', seconds: 75, frames: 4500, markers: 2, ended: 'quit' }] } };

test('with no game open the Studio starts on the games; the game tabs wait for one', async () => {
  const { app, current, tab } = await studio(listing(''));
  assert.equal(current(), 'Games');
  assert.match(app.text, /Your games 3/);
  assert.match(app.text, /Neon Vow opened just now · 2 scenes/);
  assert.match(app.text, /Gone missing/);
  assert.equal(tab('Overview').disabled, true);
});

test('with a game open it starts on its overview: scenes by folder, recent plays, the editor first', async () => {
  const { app, current, find, calls, button } = await studio(listing('/g/neon'), { answers: { plays_list: plays } });
  assert.equal(current(), 'Overview');
  assert.ok(calls().some((c) => c.name === 'plays_list'));
  assert.match(app.text, /Neon Vow/);
  const rows = find((e) => e.attributes.role === 'treeitem').map((e) => e.text.replace(/\s+/g, ' ').trim());
  assert.deepEqual(rows.slice(0, 3), ['main', '▾ levels', 'one']);
  assert.match(app.text, /1:15 · 2 markers/);
  assert.equal(button('Open in editor').attributes.class, 'primary');
});

test('a play picked with no Play tab goes to the composer, unsent', async () => {
  const { find, sent } = await studio(listing('/g/neon'), { answers: { plays_list: plays } });
  find((e) => e.attributes['data-key'] === 'tree:20261015-101500')[0].fire('click');
  await settle();
  const m = sent.filter((x) => x.method === 'ui/message').at(-1);
  assert.match(m.params.content[0].text, /my play 20261015-101500 \(1:15 long, 2 markers\)/);
});

test('tabs are switched by click and Alt+number; the host reopens the Studio where it was', async () => {
  const s = await studio(listing('/g/neon'));
  s.tab('Games').fire('click');
  assert.equal(s.current(), 'Games');
  await s.key({ altKey: true, code: 'Digit2' });
  assert.equal(s.current(), 'Overview');
  const kept = await studio(listing('/g/neon'), { widgetState: { route: '/' } });
  assert.equal(kept.current(), 'Games');
});

test('a deep link opens its page; one naming another game opens that game first', async () => {
  const s = await studio(listing('/g/neon'), { answers: { games: () => listing('/g/tetris') } });
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/?x=1' } });
  assert.equal(s.current(), 'Games');
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/game?game=/g/tetris' } });
  assert.deepEqual(s.calls().filter((c) => c.name === 'open_game'), [{ name: 'open_game', arguments: { game: '/g/tetris' } }]);
  assert.equal(s.current(), 'Overview');
  assert.match(s.app.text, /Tetris/);
});

test('the studio tool\'s route opens that page', async () => {
  const { current } = await studio(listing('/g/neon'), { input: { route: '/' } });
  assert.equal(current(), 'Games');
});

test('the host\'s theme, style variables, safe areas and display mode reach the page', async () => {
  const { root } = await studio(listing(''), { context: { theme: 'dark', displayMode: 'inline',
    styles: { variables: { '--color-background-primary': 'light-dark(#fff, #111)' } }, safeAreaInsets: { top: 12, right: 0, bottom: 4, left: 0 } } });
  assert.equal(root.dataset.theme, 'dark');
  assert.equal(root.dataset.mode, 'inline');
  assert.equal(root.style.props['--color-background-primary'], 'light-dark(#fff, #111)');
  assert.equal(root.style.props['--safe-top'], '12px');
});

test('a game picked is the model\'s context; Open opens it and shows its overview', async () => {
  const s = await studio(listing(''), { answers: { games: () => listing('/g/tetris') } });
  s.find((e) => e.attributes['data-key'] === 'card:/g/tetris')[0].fire('click');
  await settle();
  const last = s.contexts().at(-1);
  assert.equal(last.structuredContent.picked.path, '/g/tetris');
  assert.deepEqual(last.content[0].annotations, { audience: ['assistant'] });
  s.button('Open').fire('click');
  await settle(6);
  assert.deepEqual(s.calls().slice(0, 2), [{ name: 'open_game', arguments: { game: '/g/tetris' } }, { name: 'games', arguments: {} }]);
  assert.equal(s.current(), 'Overview');
  assert.equal(s.contexts().at(-1).structuredContent.studio, '/game');
});

test('a failed action says why and stays put', async () => {
  const s = await studio(listing(''), { answers: { project_export: { isError: true, content: [{ type: 'text', text: 'no engine' }] } } });
  s.find((e) => e.attributes['data-key'] === 'card:/g/neon')[0].fire('click');
  s.button('Export').fire('click');
  await settle(6);
  assert.match(s.app.text, /Couldn't: no engine/);
  assert.equal(s.current(), 'Games');
});

test('a missing game offers nothing to do', async () => {
  const s = await studio(listing(''));
  s.find((e) => e.attributes['data-key'] === 'card:/g/gone')[0].fire('click');
  assert.equal(s.button('Play'), undefined);
});

test('a new game is made from its name and opened', async () => {
  const s = await studio(listing(''), { answers: { games: () => listing('/g/star') } });
  s.button('New game').fire('click');
  s.find((e) => e.tag === 'input')[0].fire('input', { target: { value: '  Star Fox ' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(6);
  assert.deepEqual(s.calls()[0], { name: 'new_game', arguments: { name: 'Star Fox' } });
  assert.equal(s.current(), 'Overview');
});

test('an answer that isn\'t games says so', async () => {
  const { app } = await studio({ isError: true, content: [{ type: 'text', text: 'boom' }] });
  assert.match(app.text, /boom/);
});
