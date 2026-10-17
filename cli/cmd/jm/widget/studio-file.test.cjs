const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle, plain } = require('./studio-host.cjs');

const games = { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }] };
const bindingsFile = { path: 'assets/input.bindings.json', game: 'Pip', root: '/g/pip', kind: 'bindings', actions: { jump: ['Space', 'Gamepad.A'], fire: ['Z'] } };
const text = '{\n  "actions": {\n    "jump": ["Space", "Gamepad.A"],\n    "fire": ["Z"]\n  }\n}\n';
const opened = { file: { name: 'input.bindings.json', resourceUri: 'host://1' } };
const resource = { contents: [{ text, _meta: { 'openai/resource': { etag: 'v1', writable: true } } }] };

test('key bindings ChatGPT opens are edited and saved through it, as jm fmt writes them', async () => {
  const writes = [];
  const s = await studio({ structuredContent: { ...games, file: bindingsFile } }, { input: opened,
    caps: { experimental: { 'openai/resource': {} } },
    answers: { 'resources/read': resource, 'openai/resources/write': (p) => { writes.push(p); return { outcome: 'saved', etag: 'v' + (writes.length + 1) }; } } });
  await settle(6);
  assert.equal(s.current(), 'File');
  assert.match(s.app.text, /jump\s+Space\s+×\s+Gamepad\.A/);
  s.find((e) => e.attributes['aria-label'] === 'Remove Gamepad.A from jump')[0].fire('click');
  await settle(6);
  assert.equal(writes[0].text, text.replace(', "Gamepad.A"', ''));
  assert.equal(writes[0].ifMatch, 'v1');
  const add = s.find((e) => e.attributes['data-key'] === 'capture:fire')[0];
  add.fire('click');
  s.find((e) => e.attributes['data-key'] === 'capture:fire')[0].fire('keydown', { code: 'BracketLeft' });
  await settle(6);
  assert.deepEqual(plain(JSON.parse(writes[1].text).actions.fire), ['Z', 'LeftBracket']);
  assert.equal(writes[1].ifMatch, 'v2');
});

test('bindings not opened by the host are read only', async () => {
  const s = await studio({ structuredContent: { ...games, file: bindingsFile } }, { input: { path: 'assets/input.bindings.json' } });
  await settle(6);
  assert.match(s.app.text, /Read only here/);
  assert.equal(s.find((e) => e.attributes['aria-label'] === 'Remove Space from jump').length, 0);
});

test('a prefab from Index shows its entities and components; an atlas its images', async () => {
  const prefab = { path: 'assets/prefabs/cloud.prefab.json', game: 'Pip', root: '/g/pip', kind: 'prefab', entities: [
    { where: 'cloud', name: 'cloud', components: { TransformComponent: { position: [0, 0, 0] } } },
    { where: 'cloud/Shadow', name: 'Shadow', inherited: true, components: {} }] };
  const atlas = { path: 'assets/fx.atlas.json', game: 'Pip', root: '/g/pip', kind: 'atlas', images: [{ path: 'assets/fx/orb.png', region: 'orb', size: '8×8' }] };
  const s = await studio({ structuredContent: games }, { widgetState: { route: '/file?path=assets/prefabs/cloud.prefab.json' },
    answers: { file_view: (a) => a.path.endsWith('.prefab.json') ? { structuredContent: { ...games, file: prefab } }
      : { structuredContent: { ...games, file: atlas }, _meta: { 'jm/files': { 'assets/fx/orb.png': 'data:image/png;base64,orb' } } } } });
  await settle(6);
  assert.match(s.app.text, /cloud\.prefab\.json prefab/);
  assert.match(s.app.text, /"position": \[0, 0, 0\]/);
  assert.match(s.app.text, /Shadow from the prefab/);
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/file?path=assets/fx.atlas.json' } });
  await settle(6);
  assert.match(s.app.text, /orb 8×8/);
  assert.equal(s.find((e) => e.tag === 'img')[0].attributes.src, 'data:image/png;base64,orb');
});

test('the Console shows the game\'s log: files it names open in Index, a problem goes to the chat', async () => {
  const log = { log: 'logs/engine.log', counts: { error: 1, warning: 0 }, lines: [
    { time: '2026-10-08 09:47:04.080', level: 'info', text: 'started' },
    { time: '2026-10-08 09:47:05.000', level: 'error', text: '[script] assets/scripts/player.ts:12: no field' }] };
  const s = await studio({ structuredContent: games }, { widgetState: { route: '/console' }, answers: { game_log: { structuredContent: log } } });
  await settle(6);
  assert.match(s.app.text, /logs\/engine\.log · 1 errors, 0 warnings/);
  assert.match(s.app.text, /09:47:05 \[script\]\s+assets\/scripts\/player\.ts:12/);
  s.button('Ask').fire('click');
  await settle();
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').at(-1).params.content[0].text, 'The game logged an error: [script] assets/scripts/player.ts:12: no field');
  s.find((e) => e.tag === 'a')[0].fire('click');
  await settle(6);
  assert.equal(s.current(), 'Index');
});

test('a file picked after the host opened bindings isn\'t saved over them; saves wait for each other', async () => {
  const writes = [];
  const s = await studio({ structuredContent: { ...games, file: bindingsFile } }, { input: opened,
    caps: { experimental: { 'openai/resource': {} } },
    answers: { 'resources/read': resource, 'openai/resources/write': (p) => { writes.push(p); return { outcome: 'saved', etag: 'v2' }; },
      file_view: { structuredContent: { ...games, file: { ...bindingsFile, path: 'assets/other.bindings.json' } } } } });
  await settle(6);
  s.find((e) => e.attributes['aria-label'] === 'Remove Space from jump')[0].fire('click');
  s.find((e) => e.attributes['aria-label'] === 'Remove Z from fire')[0].fire('click');  // while the first saves
  await settle(6);
  assert.equal(writes.length, 1);
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/file?path=assets/other.bindings.json' } });
  await settle(6);
  assert.match(s.app.text, /other\.bindings\.json/);
  assert.match(s.app.text, /Read only here/);
  assert.equal(s.find((e) => (e.attributes['aria-label'] || '').startsWith('Remove')).length, 0);
});

// Two games of one name, A open: open_game switches which one games says is open.
const twoGames = () => {
  let open = '/g/a';
  const listing = () => ({ structuredContent: { open, games: [{ name: 'Same', path: '/g/a', opened: 2, scenes: [] }, { name: 'Same', path: '/g/b', opened: 1, scenes: [] }] } });
  return { listing, answers: { games: () => listing(), open_game: (a) => { open = a.game.replace(/\/$/, ''); return { content: [{ type: 'text', text: 'ok' }] }; } } };
};
const bindingsOf = (root, key) => ({ path: 'assets/input.bindings.json', game: 'Same', root, kind: 'bindings', actions: { jump: [key] } });
const later = () => { let release; const p = new Promise((resolve) => { release = resolve; }); return { p, release }; };

test('the same file of another game, of the same name, is that game\'s and read only', async () => {
  const g = twoGames(), writes = [];
  const held = later();
  const s = await studio({ ...g.listing(), structuredContent: { ...g.listing().structuredContent, file: bindingsOf('/g/a', 'A') } }, { input: opened,
    caps: { experimental: { 'openai/resource': {} } },
    answers: { ...g.answers, 'resources/read': { contents: [{ text: '{\n  "actions": {\n    "jump": ["A"]\n  }\n}\n', _meta: { 'openai/resource': { etag: 'a1', writable: true } } }] },
      'openai/resources/write': (p) => { writes.push(p); return { outcome: 'saved', etag: 'a2' }; },
      file_view: (a) => (a.game === '/g/b' ? { structuredContent: { file: bindingsOf('/g/b', 'B') } } : held.p) } });
  await settle(6);
  assert.ok(s.find((e) => e.attributes['aria-label'] === 'Remove A from jump').length, 'A is editable');
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/file?game=/g/b&path=assets/input.bindings.json' } });
  await settle(12);
  assert.deepEqual(s.calls().filter((c) => c.name === 'file_view').map((c) => c.arguments.game), ['/g/b']);
  assert.match(s.app.text, /jump\s+B/);
  assert.match(s.app.text, /Read only here/);
  assert.equal(s.find((e) => (e.attributes['aria-label'] || '').startsWith('Remove')).length, 0);
  assert.equal(writes.length, 0);
});

test('a host read finished after another file opened doesn\'t replace it', async () => {
  const slow = later(), writes = [];
  const fileB = { ...bindingsFile, path: 'assets/b.bindings.json' };
  const s = await studio({ structuredContent: { ...games, file: bindingsFile } }, { input: opened,
    caps: { experimental: { 'openai/resource': {} } },
    answers: { 'resources/read': (p) => (p.uri === 'host://1' ? slow.p : { contents: [{ text: text.replace('"Z"', '"B"'), _meta: { 'openai/resource': { etag: 'b1', writable: true } } }] }),
      'openai/resources/write': (p) => { writes.push(p); return { outcome: 'saved', etag: 'b2' }; } } });
  await settle(4);
  await s.notify('ui/notifications/tool-input', { arguments: { file: { name: 'b.bindings.json', resourceUri: 'host://b' } } });
  await s.notify('ui/notifications/tool-result', { structuredContent: { ...games, file: { ...fileB, actions: { jump: ['Space', 'Gamepad.A'], fire: ['B'] } } } });
  await settle(6);
  slow.release(resource);  // A's read, last
  await settle(6);
  s.find((e) => e.attributes['aria-label'] === 'Remove B from fire')[0].fire('click');
  await settle(6);
  assert.deepEqual(plain(writes.map((w) => [w.uri, w.ifMatch])), [['host://b', 'b1']]);
});

test('the Console shows only the shown game\'s log, though another\'s comes late', async () => {
  const g = twoGames();
  const slow = later();
  const logOf = (game) => ({ structuredContent: { game, log: 'logs/engine.log', counts: { error: 0, warning: 0 }, lines: [{ time: '', level: 'info', text: 'log of ' + game }] } });
  const s = await studio(g.listing(), { widgetState: { route: '/console' }, answers: { ...g.answers, game_log: (a) => (a.game === '/g/a' ? slow.p : logOf(a.game)) } });
  await settle(6);
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/console?game=/g/b' } });
  await settle(10);
  slow.release(logOf('/g/a'));  // A's, asked before the switch
  await settle(10);
  assert.doesNotMatch(s.app.text, /log of \/g\/a/);
  await new Promise((resolve) => setTimeout(resolve, 2200));  // the next poll: B's
  await settle(10);
  assert.match(s.app.text, /log of \/g\/b/);
  assert.doesNotMatch(s.app.text, /log of \/g\/a/);
  assert.ok(s.calls().filter((c) => c.name === 'game_log').every((c) => c.arguments.game), 'every poll names its game');
});

test('a selection in the log stays while new lines come, which show once it\'s gone', async () => {
  let n = 1;
  const log = () => ({ structuredContent: { game: '/g/pip', log: 'logs/engine.log', counts: { error: 0, warning: 0 },
    lines: Array.from({ length: n }, (_, i) => ({ time: '', level: 'info', text: 'line ' + i })) } });
  const s = await studio({ structuredContent: games }, { widgetState: { route: '/console' }, answers: { game_log: () => log() } });
  await settle(6);
  const line = s.find((e) => e.text === 'line 0' && e.tag === 'span')[0];
  await s.select(line);
  n = 3;
  for (let i = 0; i < 2; i++) { await new Promise((resolve) => setTimeout(resolve, 2100)); await settle(6); }
  await s.notify('ui/notifications/host-context-changed', { theme: 'dark' });  // a redraw of the whole page
  await settle(4);
  assert.doesNotMatch(s.app.text, /line 2/, 'the lines stay while selected');
  assert.equal(s.selection().anchorNode, line, 'the selection is the same');
  await s.select(null);
  assert.match(s.app.text, /line 2/);
});

test('a file the host shows from another game opens that game, then shows it', async () => {
  for (const input of [opened, { path: 'assets/input.bindings.json', game: '/g/b' }]) {
    const g = twoGames();
    const s = await studio({ structuredContent: { ...g.listing().structuredContent, file: bindingsOf('/g/b', 'B') } }, { input,
      answers: { ...g.answers, file_view: (a) => ({ structuredContent: { file: bindingsOf(a.game, a.game === '/g/b' ? 'B' : 'A') } }) } });
    await settle(12);
    assert.deepEqual(s.calls().filter((c) => c.name === 'open_game').map((c) => c.arguments.game), ['/g/b'], JSON.stringify(input));
    assert.match(s.app.text, /jump\s+B/);
  }
});

test('a route naming its game another way (a trailing slash) shows the file once that game opens', async () => {
  const g = twoGames();
  const s = await studio(g.listing(), { answers: { ...g.answers, file_view: (a) => ({ structuredContent: { file: bindingsOf(a.game, a.game === '/g/b' ? 'B' : 'A') } }) } });
  await settle(6);
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/file?game=/g/b/&path=assets/input.bindings.json' } });
  await settle(12);
  assert.match(s.app.text, /jump\s+B/);
});

test('a route\'s game that won\'t open shows its file read only, never the open game\'s', async () => {
  const g = twoGames();
  const s = await studio({ structuredContent: { ...g.listing().structuredContent, file: bindingsOf('/g/a', 'A') } }, { input: opened,
    caps: { experimental: { 'openai/resource': {} } },
    answers: { ...g.answers, open_game: { isError: true, content: [{ type: 'text', text: 'no' }] },
      'resources/read': { contents: [{ text: '{\n  "actions": {\n    "jump": ["A"]\n  }\n}\n', _meta: { 'openai/resource': { etag: 'a1', writable: true } } }] },
      file_view: (a) => ({ structuredContent: { file: bindingsOf('/g/b', 'B') } }) } });
  await settle(6);
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/file?game=/g/b&path=assets/input.bindings.json' } });
  await settle(12);
  assert.match(s.app.text, /jump\s+B/);
  assert.equal(s.find((e) => (e.attributes['aria-label'] || '').startsWith('Remove')).length, 0);
});
