const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle } = require('./studio-host.cjs');

const listing = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }] } };
const status = { structuredContent: { changes: [
  { path: 'scenes/main.scene.json', status: 'M', entities: [{ entity: 'Player', change: 'changed', changed: ['TransformComponent'] }, { entity: 'Bat', change: 'added' }] },
  { path: 'assets/scripts/bat.ts', status: '?' }] } };
const hash = (c) => c.repeat(40);
const log = { structuredContent: { versions: [
  { hash: hash('b'), subject: 'Make jumps variable height', author: 'Pat', when: 1792000000, files: { 'assets/scripts/player.ts': 'M' } },
  { hash: hash('a'), subject: 'New game', author: 'Pat', when: 1791000000, files: { '.jm.json': 'A', 'scenes/main.scene.json': 'A' } }] },
  _meta: { 'jm/thumbs': { [hash('b')]: 'data:image/jpeg;base64,b' } } };
const diff = { content: [{ type: 'text', text: 'diff --git a/x b/x\n@@ -1 +1 @@\n-old\n+new' }] };
const answers = { vcs_status: status, vcs_log: log, vcs_diff: diff, vcs_commit: { content: [{ type: 'text', text: '{"subject":"Add a bat"}' }] } };
const open = async (context) => {
  const s = await studio(listing, { answers, context });
  if (!context) s.tab('Git').fire('click');
  await settle(8);
  return s;
};
const key = (s, k) => s.find((e) => e.attributes['data-key'] === k)[0];

test('changes show per entity; a change picked shows its diff and is the context', async () => {
  const s = await open();
  assert.match(s.app.text, /Changes · 2/);
  assert.match(s.app.text, /changed Player\s+TransformComponent/);
  assert.match(s.app.text, /added Bat/);
  key(s, 'tree:scenes/main.scene.json').fire('click');
  await settle(6);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_diff').arguments, { file: 'scenes/main.scene.json' });
  assert.ok(s.find((e) => e.attributes.class === 'git-add')[0].text.startsWith('+new'));
  assert.equal(s.contexts().at(-1).structuredContent.change, 'scenes/main.scene.json');
});

test('commit sends the message; discard asks first and keeps a stash', async () => {
  const s = await open();
  key(s, 'git-message').fire('input', { target: { value: ' Add a bat ' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_commit').arguments, { message: 'Add a bat' });
  assert.match(s.app.text, /Committed: Add a bat\./);
  key(s, 'tree:assets/scripts/bat.ts').fire('click');
  await settle(6);
  s.button('Discard').fire('click');
  assert.equal(s.calls().filter((c) => c.name === 'vcs_restore').length, 0);
  s.button('Discard? (kept in the stash)').fire('click');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_restore').arguments, { files: ['assets/scripts/bat.ts'] });
});

test('history has thumbnails; a version can be gone back to, or one file restored', async () => {
  const s = await open();
  assert.equal(s.find((e) => e.tag === 'img')[0].attributes.src, 'data:image/jpeg;base64,b');
  key(s, 'version:' + hash('a')).fire('click');
  assert.match(s.app.text, /New game/);
  s.button('Go back to this').fire('click');
  s.button('Go back? (changes kept in the stash)').fire('click');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_restore').arguments, { files: ['.'], from: hash('a') });
});

test('a deep link with a short hash opens that version', async () => {
  const s = await open({ 'openai/deepLink': { url: '/git?version=bbbbbbbb' } });
  assert.equal(s.current(), 'Git');
  assert.match(s.app.text, /Go back to this/);
  assert.equal(s.contexts().at(-1).structuredContent.version, hash('b'));
});

test('a game with no history offers to start it', async () => {
  const s = await studio(listing, { answers: { vcs_status: { structuredContent: { changes: [] } }, vcs_log: { isError: true, content: [{ type: 'text', text: 'no history' }] } } });
  s.tab('Git').fire('click');
  await settle(8);
  assert.match(s.app.text, /No history yet/);
  assert.equal(s.button('Start the history').disabled, false);
});
