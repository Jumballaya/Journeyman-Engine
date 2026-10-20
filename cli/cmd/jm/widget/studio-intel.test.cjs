const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle, plain } = require('./studio-host.cjs');

const games = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: ['scenes/a.scene.json'] }] } };
const entries = [
  { path: 'assets/hero.png', kind: 'texture', detail: '16×16', uses: [], usedBy: ['assets/prefabs/hero.prefab.json'] },
  { path: 'assets/old.png', kind: 'texture', detail: '8×8', uses: [], usedBy: [], unused: true },
  { path: 'assets/prefabs/hero.prefab.json', kind: 'prefab', uses: ['assets/hero.png'], usedBy: ['scenes/a.scene.json'] },
  { path: 'scenes/a.scene.json', kind: 'scene', detail: '3 entities', uses: ['assets/prefabs/hero.prefab.json'], usedBy: ['.jm.json'] },
];
const index = (args) => args.path ? { content: [{ type: 'text', text: args.path }], _meta: { 'jm/image': 'data:image/png;base64,hero' } }
  : { content: [{ type: 'text', text: 'Pip' }], structuredContent: { game: 'Pip', entries } };
const key = (s, k) => s.find((e) => e.attributes['data-key'] === k)[0];

test('the Index lists files by kind; a file shows what it uses, what uses it, and its picture', async () => {
  const s = await studio(games, { answers: { game_index: index }, widgetState: { route: '/index' } });
  await settle(6);
  assert.match(s.app.text, /texture 2/);
  key(s, 'tree:assets/hero.png').fire('click');
  await settle(6);
  assert.match(s.app.text, /hero\.png texture assets\/hero\.png · 16×16/);
  assert.match(s.app.text, /Used by \(1\)\s+assets\/prefabs\/hero\.prefab\.json/);
  assert.equal(s.find((e) => e.tag === 'img')[0].attributes.src, 'data:image/png;base64,hero');
  assert.deepEqual(plain(s.contexts().at(-1).structuredContent.usedBy), ['assets/prefabs/hero.prefab.json']);
  key(s, 'tree:assets/prefabs/hero.prefab.json').fire('click');  // its used-by link
  await settle(6);
  assert.match(s.app.text, /hero\.prefab\.json prefab/);
});

test('"unused" finds what nothing uses; a link picks a file', async () => {
  const s = await studio(games, { answers: { game_index: index }, widgetState: { route: '/index?path=scenes/a.scene.json' } });
  await settle(6);
  assert.match(s.app.text, /a\.scene\.json scene/);
  assert.ok(s.button('Show in editor'));
  key(s, 'index-find').fire('input', { target: { value: 'unused' } });
  const rows = s.find((e) => e.attributes.role === 'treeitem').map((e) => e.attributes['data-key']);
  assert.ok(rows.includes('tree:assets/old.png') && !rows.includes('tree:assets/hero.png'));
});

test('Health\'s art card lists each sprite, how far along, one to the chat', async () => {
  const health = (args) => ({ structuredContent: { checks: [args.checks[0] !== 'art' ? { name: args.checks[0], ok: true, summary: 'ok', problems: [] }
    : { name: 'art', ok: false, summary: '1 thing(s) off design/Art.md; 1 of 2 sprites done', problems: [{ level: 'warning', message: 'no walk_up tag', file: 'assets/knight.json' }],
        art: { assets: [{ title: 'Knight', note: 'design/Characters/Knight.md', set: 'character', status: 'exported', sprite: 'assets/knight.aseprite', problems: 1 },
                        { title: 'Mira', note: 'design/Characters/Mira.md', set: 'boss', status: 'done', problems: 0 }] } }] } });
  const s = await studio(games, { answers: { health }, widgetState: { route: '/health' } });
  await settle(2);
  s.button('Check all').fire('click');
  await settle(18);
  assert.match(s.app.text, /exported\s*Knight\s*· assets\/knight.aseprite · 1 off/);
  assert.match(s.app.text, /done\s*Mira\s*· design\/Characters\/Mira.md/);
  s.find((e) => e.attributes.title === 'Ask your agent about it')[0].fire('click');
  await settle();
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').at(-1).params.content[0].text,
    'The sprite Knight (design/Characters/Knight.md) is exported, 1 thing(s) off the art note: ');
});

test('Health runs each check and lists its problems, each one to the chat', async () => {
  const asked = [];
  const health = (args) => {
    asked.push(args.checks[0]);
    const failing = args.checks[0] === 'build';
    return { structuredContent: { checks: [{ name: args.checks[0], ok: !failing, summary: failing ? '1 error' : 'ok',
      problems: failing ? [{ level: 'error', message: 'no such prefab', file: 'scenes/a.scene.json', line: 4 }] : [] }] } };
  };
  const s = await studio(games, { answers: { health }, widgetState: { route: '/health' } });
  await settle(2);
  s.button('Check all').fire('click');
  await settle(18);
  assert.deepEqual(asked, ['doctor', 'build', 'tests', 'goldens', 'world', 'art']);
  assert.match(s.app.text, /Build failing 1 error/);
  s.find((e) => e.attributes.title === 'Ask your agent about it')[0].fire('click');
  await settle();
  assert.equal(s.sent.filter((m) => m.method === 'ui/message').at(-1).params.content[0].text, 'Build says error at scenes/a.scene.json:4: no such prefab');
  assert.match(s.contexts().at(-1).content[0].text, /Health of Pip: Setup ok; Build 1 error/);
});
