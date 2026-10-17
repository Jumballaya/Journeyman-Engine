const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle, plain } = require('./studio-host.cjs');

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

test('the studio tool\'s route opens that page, each time it\'s called', async () => {
  const s = await studio(listing('/g/neon'), { input: { route: '/' } });
  assert.equal(s.current(), 'Games');
  s.tab('Overview').fire('click');
  await s.notify('ui/notifications/tool-input', { arguments: { route: '/' } });
  assert.equal(s.current(), 'Games');
});

test('a route naming a game before the games are known opens it once they are', async () => {
  const s = await studio(listing('/g/neon'), { input: { route: '/game?game=tetris' }, answers: { games: () => listing('/g/tetris') } });
  await settle(6);
  assert.deepEqual(s.calls().filter((c) => c.name === 'open_game'), [{ name: 'open_game', arguments: { game: 'tetris' } }]);
  assert.match(s.app.text, /Tetris/);
});

test('context a host refused isn\'t sent again and again', async () => {
  const s = await studio(listing(''), { answers: {}, refuseContext: true });
  await settle(10);
  assert.equal(s.contexts().length, 1);
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
  assert.equal(last.content[0]._meta['openai/title'], 'Tetris');  // what they picked: an item in their chat
  assert.deepEqual(last.content[1].annotations, { audience: ['assistant'] });
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

test('the game goes into the person\'s draft, here or in a new chat, titled with its picture', async () => {
  const s = await studio(listing('/g/neon'), { caps: { experimental: { 'openai/message': {} } } });
  s.button('New chat').fire('click');
  await settle();
  const m = plain(s.sent.filter((x) => x.method === 'ui/message').at(-1).params);
  assert.deepEqual(m._meta, { 'openai/message': { target: 'new', send: false } });
  assert.equal(m.content[0]._meta['openai/title'], 'Neon Vow');
  assert.equal(m.content[1].type, 'image');
  s.button('Ask about it').fire('click');
  await settle();
  assert.equal(s.sent.filter((x) => x.method === 'ui/message').at(-1).params._meta['openai/message'].target, 'active');
});

test('a host without drafts gets the question, and no New chat', async () => {
  const s = await studio(listing('/g/neon'));
  assert.equal(s.button('New chat'), undefined);
  s.button('Ask about it').fire('click');
  await settle();
  assert.equal(s.sent.filter((x) => x.method === 'ui/message').at(-1).params.content[0].text, 'Let\'s work on my game Neon Vow.');
});

test('where the host shows forms, New game lets new_game ask, then shows the game', async () => {
  const withForms = (open) => ({ ...listing(open), _meta: { ...listing(open)._meta, 'jm/forms': true } });
  const s = await studio(withForms(''), { answers: { games: () => withForms('/g/new'),
    new_game: { content: [{ type: 'text', text: 'Opened /g/new.' }], structuredContent: { made: '/g/new' } } } });
  s.button('New game').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().slice(0, 2), [{ name: 'new_game', arguments: {} }, { name: 'open_game', arguments: { game: '/g/new' } }]);
  assert.equal(s.current(), 'Overview');
});

test('a game named by its folder name is opened, even when one of that name elsewhere is open', async () => {
  const outside = listing('/outside/neon');
  const s = await studio(outside, { answers: { games: () => listing('/g/neon') } });
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/game?game=neon' } });
  await settle(6);
  assert.deepEqual(s.calls().filter((c) => c.name === 'open_game'), [{ name: 'open_game', arguments: { game: 'neon' } }]);
  // Named outright, it's opened even when the listing says it's open: another caller may have opened another since.
  const t = await studio(listing('/g/neon'));
  await t.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/game?game=/g/neon' } });
  await settle(6);
  assert.deepEqual(t.calls().filter((c) => c.name === 'open_game'), [{ name: 'open_game', arguments: { game: '/g/neon' } }]);
});

test('the overview reads its own game\'s plays, and drops an answer about another', async () => {
  const other = { structuredContent: { game: '/g/tetris', plays: [{ id: 'tetris-play', started: '2026-10-15T10:15:00Z', seconds: 5, markers: 0 }] } };
  const s = await studio(listing('/g/neon'), { answers: { plays_list: other } });
  await settle(6);
  assert.deepEqual(s.calls().find((c) => c.name === 'plays_list').arguments, { game: '/g/neon' });
  assert.doesNotMatch(s.app.text, /tetris-play/);
});

test('a host that refused context once isn\'t asked again, whatever happens next', async () => {
  const s = await studio(listing('/g/neon'), { refuseContext: true, answers: { plays_list: plays } });
  await settle(10);
  await s.notify('ui/notifications/host-context-changed', { theme: 'dark' });
  s.tab('Games').fire('click');
  s.find((e) => e.attributes['data-key'] === 'card:/g/tetris')[0].fire('click');
  await settle(10);
  assert.equal(s.contexts().length, 1);
});

test('reopened, the Studio stays where the person went, not where its tool first sent it', async () => {
  const first = await studio(listing('/g/neon'), { input: { route: '/' } });
  assert.equal(first.current(), 'Games');
  const kept = { route: '/game', seen: { input: JSON.stringify({ route: '/' }) } };
  const s = await studio(listing('/g/neon'), { input: { route: '/' }, widgetState: kept });
  assert.equal(s.current(), 'Overview');
  const saved = s.sent.length && JSON.parse(JSON.stringify(s.widgetState()));
  const again = await studio(listing('/g/neon'), { input: { route: '/' }, widgetState: saved });
  assert.equal(again.current(), 'Overview', 'reopened twice');
  await s.notify('ui/notifications/tool-input', { arguments: { route: '/', again: 1 } });
  assert.equal(s.current(), 'Games', 'a new call still moves it');
  const other = await studio(listing('/g/neon'), { widgetState: { route: '/game?game=/g/tetris' }, answers: { games: () => listing('/g/tetris') } });
  await settle(6);
  assert.deepEqual(other.calls().filter((c) => c.name === 'open_game'), [{ name: 'open_game', arguments: { game: '/g/tetris' } }]);
});

test('a late plays answer for another game or an older visit doesn\'t replace the shown one', async () => {
  const held = [];
  const playsOf = (game, seconds) => ({ structuredContent: { game, plays: [{ id: game, started: '2026-10-15T10:15:00Z', seconds, markers: 0 }] } });
  let open = '/g/neon';
  const s = await studio(listing('/g/neon'), { answers: {
    games: () => listing(open),
    open_game: (a) => { open = a.game; return { content: [{ type: 'text', text: 'ok' }] }; },
    plays_list: (a) => (a.game === '/g/neon' ? new Promise((resolve) => held.push(() => resolve(playsOf('/g/neon', 75)))) : playsOf(a.game, 5)) } });
  await settle(6);
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/game?game=/g/tetris' } });
  await settle(10);
  assert.match(s.app.text, /0:05/);
  for (const release of held.splice(0)) release();
  await settle(10);
  assert.match(s.app.text, /0:05/, "Tetris's plays");
  assert.doesNotMatch(s.app.text, /1:15|Reading the plays/);
});

test('Show opens the game even when it\'s listed as open, and stays put when that fails', async () => {
  let refuse = false;
  const s = await studio(listing('/g/neon'), { widgetState: { route: '/' }, answers: {
    open_game: () => (refuse ? { isError: true, content: [{ type: 'text', text: 'gone' }] } : { content: [{ type: 'text', text: 'ok' }] }) } });
  s.find((e) => e.attributes['data-key'] === 'card:/g/neon')[0].fire('click');
  s.button('Show').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().filter((c) => c.name === 'open_game'), [{ name: 'open_game', arguments: { game: '/g/neon' } }]);
  assert.equal(s.current(), 'Overview');
  refuse = true;
  s.tab('Games').fire('click');
  s.find((e) => e.attributes['data-key'] === 'card:/g/neon')[0].fire('click');
  s.button('Show').fire('click');
  await settle(8);
  assert.equal(s.current(), 'Games');
});

test('with no games, the Studio looks for them once by itself; Find my games looks again', async () => {
  const none = { structuredContent: { open: '', games: [] } };
  const s = await studio(none, { answers: { games: () => none, games_find: { content: [{ type: 'text', text: 'Found 0 new games.' }] } } });
  await settle(10);
  assert.equal(s.calls().filter((c) => c.name === 'games_find').length, 1);
  assert.match(s.app.text, /Find my games/);
  s.button('Find my games').fire('click');
  await settle(8);
  assert.equal(s.calls().filter((c) => c.name === 'games_find').length, 2);
});
