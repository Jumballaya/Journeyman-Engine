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

test('a marker answered after its run ended, or after the Studio moved to another game, drafts nothing', async () => {
  const two = (open) => ({ structuredContent: { open, games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }, { name: 'Bo', path: '/g/bo', opened: 1, scenes: [] }] } });
  let open = '/g/pip', run = '1', hold = false;
  const held = [];
  const reply = (args) => ({ structuredContent: { game: args.game, run, answers: (args.commands || [args.command]).map((c) => c.startsWith('marker') ? { ok: true, marker: 7 } : { ok: true, frame: Number(run), scene: 's', speed: 1 }) } });
  const s = await studio(two(open), { widgetState: { route: '/play' }, caps: { experimental: { 'openai/message': {} } }, answers: {
    games: () => two(open),
    open_game: (a) => { open = a.game; return { content: [{ type: 'text', text: 'ok' }] }; },
    play_live: (args) => (hold && (args.command || '').startsWith('marker') ? new Promise((resolve) => held.push(() => resolve(reply(args))))
      : args.run && args.run !== run ? { isError: true, content: [{ type: 'text', text: "The game isn't running" }] } : reply(args)) } });
  await tick(); await settle(6);
  hold = true;
  s.button('Mark').fire('click');
  await settle(3);
  run = '2';  // run 1 ended; the next poll finds run 2
  await tick(); await settle(6);
  await tick(); await settle(6);
  for (const r of held.splice(0)) r();
  await settle(6);
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').length, 0, 'no draft from run 1');
  await tick(); await settle(6);  // a poll after run 2 was found
  assert.equal(s.calls().filter((c) => c.name === 'play_live').at(-1).arguments.run, '2');
  s.button('Mark').fire('click');
  await settle(3);
  await s.notify('ui/notifications/host-context-changed', { 'openai/deepLink': { url: '/game?game=/g/bo' } });
  await settle(10);
  for (const r of held.splice(0)) r();
  await settle(6);
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').length, 0, 'no draft of Pip\'s under Bo');
});

test('the picture keeps moving with a control focused, and a value being typed stays', async () => {
  let frame = 1, pace = 1;
  const one = (c) => c === 'get session.score' ? { ok: true, value: 42 }
    : c.startsWith('speed ') ? (pace = Number(c.slice(6)), { ok: true }) : { ok: true, frame: frame++, scene: 's', speed: pace };
  const s = await studio(games, { widgetState: { route: '/play' }, answers: { play_live: (args) => ({ structuredContent: { game: '/g/pip', run: '1',
    answers: (args.commands || [args.command]).map(one) } }) } });
  await tick(); await settle(6);
  s.find((e) => e.attributes['data-key'] === 'watch-add')[0].fire('keydown', { key: 'Enter', target: { value: 'session.score' } });
  await tick(); await settle(6);
  const speed = s.find((e) => e.attributes['data-key'] === 'speed')[0];
  speed.focus();
  speed.fire('click');
  s.find((e) => e.attributes['data-key'] === 'speed')[0].fire('click');  // before the next poll
  await settle();  // calls go out once the host is ready
  const asked = s.calls().filter((c) => c.name === 'play_live' && /^speed /.test(c.arguments.command || '')).map((c) => c.arguments.command);
  assert.deepEqual(asked, ['speed 0.5', 'speed 0.25'], 'each click goes one speed further');
  await tick(); await settle(6);  // the next poll agrees
  assert.match(s.find((e) => e.attributes['data-key'] === 'speed')[0].text, /0\.25×/, 'the button shows the speed asked');
  const before = s.app.text.match(/frame (\d+)/)[1];
  await tick(); await settle(6);
  assert.notEqual(s.app.text.match(/frame (\d+)/)[1], before, 'the frame moved on');
  const field = s.find((e) => e.attributes['data-key'] === 'watch:session.score')[0];
  field.fire('input', { target: { value: '100' } });
  await tick(); await settle(6);
  const redrawn = s.find((e) => e.attributes['data-key'] === 'watch:session.score')[0];
  assert.equal(redrawn.attributes.value, '100');
  const keyless = s.find((e) => ['button', 'input', 'select'].includes(e.tag) && !e.attributes['data-key']);
  assert.equal(keyless.length, 0, 'every control gets its focus back after a redraw');
  redrawn.fire('blur');  // leaving the redrawn field sends the draft
  await tick();
  assert.ok(s.calls().some((c) => c.name === 'play_live' && c.arguments.command === 'set score 100'));
});

test('a session value being typed isn\'t set by the polls redrawing it, only when the person leaves it', async () => {
  let frame = 1;
  const one = (c) => c === 'get session.score' ? { ok: true, value: 42 } : c.startsWith('set ') ? { ok: true } : { ok: true, frame: frame++, scene: 's', speed: 1 };
  const s = await studio(games, { widgetState: { route: '/play' }, answers: { play_live: (args) => ({ structuredContent: { game: '/g/pip', run: '1',
    answers: (args.commands || [args.command]).map(one) } }) } });
  await tick(); await settle(6);
  s.find((e) => e.attributes['data-key'] === 'watch-add')[0].fire('keydown', { key: 'Enter', target: { value: 'session.score' } });
  await tick(); await settle(6);
  const sets = () => s.calls().filter((c) => c.name === 'play_live' && /^set /.test(c.arguments.command || '')).map((c) => c.arguments);
  const field = () => s.find((e) => e.attributes['data-key'] === 'watch:session.score')[0];
  for (const range of [null, [0, 1]]) {
    const before = sets().length;
    const f = field();
    f.focus();
    if (range) [f.selectionStart, f.selectionEnd] = range;
    f.value = '9';  // typed into it
    f.fire('input', { target: { value: '9' } });
    const frameAt = s.app.text.match(/frame (\d+)/)[1];
    await tick(); await settle(6);
    await tick(); await settle(6);  // two polls redraw the field it's in
    assert.equal(sets().length, before, 'no set while typing');
    assert.notEqual(s.app.text.match(/frame (\d+)/)[1], frameAt, 'the picture moved on');
    assert.equal(field().attributes.value, '9', 'the draft stays');
    field().fire('input', { target: { value: '99' } });
    field().fire('change', { target: { value: '99' } });  // the person leaves it
    field().fire('blur');
    await tick(); await settle(6);
    await tick(); await settle(6);
    assert.deepEqual(plain(sets().slice(before)), [{ command: 'set score 99', game: '/g/pip', run: '1' }], range ? 'with a selection' : 'once');
  }
});

test('beside the chat: Play asks for picture-in-picture, then shows the game and its controls only', async () => {
  const asked = [];
  const s = await studio(games, { answers: { play_live: running(asked) }, widgetState: { route: '/play' },
    context: { availableDisplayModes: ['inline', 'fullscreen', 'pip'] } });
  await tick(); await settle(6);
  s.button('Beside the chat').fire('click');
  await settle(4);
  assert.equal(plain(s.sent.filter((m) => m.method === 'ui/request-display-mode').at(-1).params).mode, 'pip');
  await s.notify('ui/notifications/host-context-changed', { displayMode: 'pip' });
  await tick(); await settle(6);
  assert.equal(s.root.dataset.bar, 'none');
  assert.ok(s.button('Back to Studio'));
  assert.ok(s.button('Pause'));
  assert.equal(s.find((e) => e.attributes['data-key'] === 'watch-add').length, 0, 'no watch list beside the chat');
  await s.key({ altKey: true, code: 'Digit2' });
  assert.equal(s.current(), 'Play', 'beside the chat, the page shown stays');
  s.button('Back to Studio').fire('click');
  await settle(4);
  assert.equal(plain(s.sent.filter((m) => m.method === 'ui/request-display-mode').at(-1).params).mode, 'fullscreen');
});
