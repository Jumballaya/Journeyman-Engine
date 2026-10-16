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
  assert.deepEqual(plain(asked.at(-1)), { command: 'pause', game: '/g/pip' });
  s.find((e) => e.attributes['data-key'] === 'watch-add')[0].fire('keydown', { key: 'Enter', target: { value: 'session.score' } });
  await tick(); await settle(6);
  assert.deepEqual(plain(asked.at(-1).commands), ['status', 'get session.score']);
  const field = s.find((e) => e.attributes['data-key'] === 'watch:session.score')[0];
  assert.equal(field.attributes.value, '42');
  field.fire('change', { target: { value: '100' } });
  await settle(6);
  assert.deepEqual(plain(asked.at(-1)), { command: 'set score 100', game: '/g/pip' });
});

test('a moment marked with a note is drafted in the chat', async () => {
  const asked = [];
  const s = await studio(games, { answers: { play_live: running(asked) }, widgetState: { route: '/play' }, caps: { experimental: { 'openai/message': {} } } });
  await tick(); await settle(6);
  const note = s.find((e) => e.attributes['data-key'] === 'mark-note')[0];
  note.fire('input', { target: { value: 'the jump is floaty' } });
  note.fire('keydown', { key: 'Enter' });
  await settle(6);
  assert.deepEqual(plain(asked.at(-1)), { command: 'marker the jump is floaty', game: '/g/pip' });
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').at(-1).params.content[0].text, 'In my play just now (marker m2): the jump is floaty');
});

test('another game\'s or an earlier ask\'s answers never show: watches keep their own values', async () => {
  const two = (open) => ({ structuredContent: { open, games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }, { name: 'Bo', path: '/g/bo', opened: 1, scenes: [] }] } });
  let open = '/g/pip';
  const held = [];
  const answer = (args) => {
    const one = (c) => c === 'status' ? { ok: true, frame: args.game === '/g/pip' ? 111 : 222, scene: 's', paused: false, speed: 1 }
      : c === 'get session.score' ? { ok: true, value: 11 } : c === 'get session.lives' ? { ok: true, value: 99 } : { ok: true };
    return { structuredContent: { game: args.game, run: '1', answers: (args.commands || [args.command]).map(one) } };
  };
  const s = await studio(two(open), { widgetState: { route: '/play' }, answers: {
    games: () => two(open),
    open_game: (a) => { open = a.game; return { content: [{ type: 'text', text: 'ok' }] }; },
    play_live: (args) => new Promise((resolve) => held.push(() => resolve(answer(args)))) } });
  const release = async () => { for (const r of held.splice(0)) r(); await settle(6); };
  await tick(); await release();
  const add = (w) => s.find((e) => e.attributes['data-key'] === 'watch-add')[0].fire('keydown', { key: 'Enter', target: { value: w } });
  add('session.score'); add('session.lives');
  await tick();  // a poll asking for both, held
  const removeScore = s.find((e) => e.attributes.title === 'Stop watching')[0];
  removeScore.fire('click');
  await release();
  const lives = s.find((e) => e.attributes['data-key'] === 'watch:session.lives')[0];
  assert.equal(lives.attributes.value, '99', 'lives shows its own value');
  await tick();  // a poll for Pip, held
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/play?game=/g/bo' } });
  await settle(10);
  await release();
  assert.doesNotMatch(s.app.text, /frame 111/);
  await tick(); await release();
  assert.match(s.app.text, /frame 222/);
  assert.ok(s.calls().filter((c) => c.name === 'play_live').every((c) => c.arguments.game));
});

test('the frame-time strip draws a row a part', async () => {
  const asked = [];
  const profiled = (args) => {
    const r = running(asked)(args);
    if ((args.commands || []).includes('profile')) r.structuredContent.answers[r.structuredContent.answers.length - 1] = { ok: true, profile: { frame: { avgMs: 3, worstMs: 9 }, systems: [{ name: 'Physics', avgMs: 1, worstMs: 2 }], scripts: [], modules: [] } };
    return r;
  };
  const s = await studio(games, { answers: { play_live: profiled }, widgetState: { route: '/play' } });
  await tick(); await settle(6);
  s.find((e) => e.tag === 'input' && e.attributes.type === 'checkbox')[0].fire('change', { target: { checked: true } });
  await tick(); await settle(6);
  assert.match(s.app.text, /Physics/);
  assert.doesNotMatch(s.app.text, /\[object/);
});

test('a run that ended isn\'t followed: the next one is found', async () => {
  let run = '1';
  const s = await studio(games, { widgetState: { route: '/play' }, answers: { play_live: (args) => (args.run && args.run !== run
    ? { isError: true, content: [{ type: 'text', text: "The game isn't running" }] }
    : { structuredContent: { game: '/g/pip', run, answers: (args.commands || [args.command]).map(() => ({ ok: true, frame: Number(run), scene: 's', speed: 1 })) } }) } });
  await tick(); await settle(6);
  run = '2';  // closed, and played again
  await tick(); await settle(6);
  await tick(); await settle(6);
  assert.match(s.app.text, /frame 2/);
});
