const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle, plain } = require('./studio-host.cjs');

const games = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: ['scenes/level.scene.json'] }] } };
const tick = () => new Promise((resolve) => setTimeout(resolve, 760));
// A running game: answers each command as the engine would.
const running = (asked) => (args) => {
  asked.push(args);
  const one = (c) => c === 'status' ? { ok: true, frame: 120, scene: 'scenes/level.scene.json', paused: false, speed: 1 }
    : c === 'get session.score' ? { ok: true, value: 42 }
    : c.startsWith('marker') ? { ok: true, marker: 2 }
    : { ok: true };
  const answers = (args.commands || [args.command]).map(one);
  return { content: [{ type: 'text', text: 'ok' }], structuredContent: { answers }, _meta: args.frame ? { 'jm/image': 'data:image/jpeg;base64,now' } : undefined };
};

test('with no game running, Play starts one', async () => {
  const s = await studio(games, { answers: { play_live: { isError: true, content: [{ type: 'text', text: "The game isn't running" }] } }, widgetState: { route: '/play' } });
  await tick(); await settle(6);
  assert.match(s.app.text, /The game isn't running/);
  s.button('Play').fire('click');
  await settle(6);
  assert.ok(s.calls().some((c) => c.name === 'project_play'));
});

test('the Play tab follows the running game: its picture, pause, watched values', async () => {
  const asked = [];
  const s = await studio(games, { answers: { play_live: running(asked) }, widgetState: { route: '/play' } });
  await tick(); await settle(6);
  assert.equal(s.find((e) => e.tag === 'img')[0].attributes.src, 'data:image/jpeg;base64,now');
  assert.match(s.app.text, /level · frame 120/);
  s.button('Pause').fire('click');
  await settle(6);
  assert.deepEqual(plain(asked.at(-1)), { command: 'pause' });
  s.find((e) => e.attributes['data-key'] === 'watch-add')[0].fire('keydown', { key: 'Enter', target: { value: 'session.score' } });
  await tick(); await settle(6);
  assert.deepEqual(plain(asked.at(-1).commands), ['status', 'get session.score']);
  const field = s.find((e) => e.attributes['data-key'] === 'watch:session.score')[0];
  assert.equal(field.attributes.value, '42');
  field.fire('change', { target: { value: '100' } });
  await settle(6);
  assert.deepEqual(plain(asked.at(-1)), { command: 'set score 100' });
});

test('a moment marked with a note is drafted in the chat', async () => {
  const asked = [];
  const s = await studio(games, { answers: { play_live: running(asked) }, widgetState: { route: '/play' }, caps: { experimental: { 'openai/message': {} } } });
  await tick(); await settle(6);
  const note = s.find((e) => e.attributes['data-key'] === 'mark-note')[0];
  note.fire('input', { target: { value: 'the jump is floaty' } });
  note.fire('keydown', { key: 'Enter' });
  await settle(6);
  assert.deepEqual(plain(asked.at(-1)), { command: 'marker the jump is floaty' });
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').at(-1).params.content[0].text, 'In my play just now (marker m2): the jump is floaty');
});
