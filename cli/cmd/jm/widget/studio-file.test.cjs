const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle, plain } = require('./studio-host.cjs');

const games = { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }] };
const bindingsFile = { path: 'assets/input.bindings.json', game: 'Pip', kind: 'bindings', actions: { jump: ['Space', 'Gamepad.A'], fire: ['Z'] } };
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
  const prefab = { path: 'assets/prefabs/cloud.prefab.json', game: 'Pip', kind: 'prefab', entities: [
    { where: 'cloud', name: 'cloud', components: { TransformComponent: { position: [0, 0, 0] } } },
    { where: 'cloud/Shadow', name: 'Shadow', inherited: true, components: {} }] };
  const atlas = { path: 'assets/fx.atlas.json', game: 'Pip', kind: 'atlas', images: [{ path: 'assets/fx/orb.png', region: 'orb', size: '8×8' }] };
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
