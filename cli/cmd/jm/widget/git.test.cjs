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
const diffText = 'diff --git a/x b/x\n@@ -1 +1 @@\n-old\n+new';
const diff = (a) => ({ content: [{ type: 'text', text: diffText }], structuredContent: { file: a.file, diff: diffText } });
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
  assert.match(s.app.text, /changed "Player"\s+TransformComponent/);
  assert.match(s.app.text, /added "Bat"/);
  key(s, 'tree:scenes/main.scene.json').fire('click');
  await settle(6);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_diff').arguments, { file: 'scenes/main.scene.json', game: '/g/pip' });
  assert.ok(s.find((e) => e.attributes.class === 'git-add')[0].text.startsWith('+new'));
  assert.equal(s.contexts().at(-1).structuredContent.change, 'scenes/main.scene.json');
});

test('commit sends the message; discard asks first and keeps a stash', async () => {
  const s = await open();
  key(s, 'git-message').fire('input', { target: { value: ' Add a bat ' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_commit').arguments, { message: 'Add a bat', game: '/g/pip' });
  assert.match(s.app.text, /Committed: Add a bat\./);
  key(s, 'tree:assets/scripts/bat.ts').fire('click');
  await settle(6);
  s.button('Discard').fire('click');
  assert.equal(s.calls().filter((c) => c.name === 'vcs_restore').length, 0);
  s.button('Discard? (kept in the stash)').fire('click');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_restore').arguments, { files: ['assets/scripts/bat.ts'], game: '/g/pip' });
});

test('history has thumbnails; a version can be gone back to, or one file restored', async () => {
  const s = await open();
  assert.equal(s.find((e) => e.tag === 'img')[0].attributes.src, 'data:image/jpeg;base64,b');
  key(s, 'version:' + hash('a')).fire('click');
  assert.match(s.app.text, /New game/);
  s.button('Go back to this').fire('click');
  s.button('Go back? (changes kept in the stash)').fire('click');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_restore').arguments, { files: ['.'], from: hash('a'), game: '/g/pip' });
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

test('before the first commit, the new files are listed to look at', async () => {
  const s = await studio(listing, { answers: { ...answers, vcs_status: { structuredContent: { changes: [{ path: '.jm.json', status: '?' }] } },
    vcs_log: { isError: true, content: [{ type: 'text', text: 'no history' }] } } });
  s.tab('Git').fire('click');
  await settle(8);
  assert.match(s.app.text, /No history yet/);
  assert.match(s.app.text, /Changes · 1/);
  assert.match(s.app.text, /\.jm\.json/);
});

test('a file picked shows its own diff, never another file\'s', async () => {
  const other = (a) => ({ content: [{ type: 'text', text: 'diff --git a/scenes/main.scene.json' }], structuredContent: { file: 'scenes/main.scene.json', diff: '+scene' } });
  const s = await studio(listing, { answers: { ...answers, vcs_diff: (a) => (a.file === 'assets/scripts/bat.ts' ? other(a) : diff(a)) } });
  s.tab('Git').fire('click');
  await settle(8);
  key(s, 'tree:assets/scripts/bat.ts').fire('click');
  await settle(6);
  assert.doesNotMatch(s.app.text, /\+scene/);
  assert.match(s.app.text, /Couldn't read the diff/);
  key(s, 'tree:scenes/main.scene.json').fire('click');
  await settle(6);
  assert.match(s.app.text, /\+new/);
});

test('a commit that fails keeps its message to try again', async () => {
  let refuse = true;
  const s = await studio(listing, { answers: { ...answers, vcs_commit: () => (refuse ? { isError: true, content: [{ type: 'text', text: 'no identity' }] } : answers.vcs_commit) } });
  s.tab('Git').fire('click');
  await settle(8);
  key(s, 'git-message').fire('input', { target: { value: 'Add a bat' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.equal(key(s, 'git-message').value, 'Add a bat');
  refuse = false;
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.equal(s.calls().filter((c) => c.name === 'vcs_commit').at(-1).arguments.message, 'Add a bat');
  assert.equal(key(s, 'git-message').value, '');
});

test('an entity under a file with "#" in its name or folder picks that whole file; a second Coin is told apart', async () => {
  for (const path of ['scenes/main#bonus.scene.json', 'sce#nes/a#b#c.scene.json']) {
    const changes = { structuredContent: { changes: [
      { path, status: 'M', entities: [{ entity: 'Coin', change: 'changed' }, { entity: 'Coin', occurrence: 2, change: 'removed' }] },
      { path: 'scenes/main', status: 'M' }] } };
    const s = await studio(listing, { answers: { ...answers, vcs_status: changes } });
    s.tab('Git').fire('click');
    await settle(8);
    assert.match(s.app.text, /changed "Coin"/);
    assert.match(s.app.text, /removed "Coin" #2/);
    key(s, 'tree:' + path + '#1').fire('click');
    await settle(6);
    assert.equal(s.calls().filter((c) => c.name === 'vcs_diff').at(-1).arguments.file, path);
    s.button('Discard').fire('click');
    s.button('Discard? (kept in the stash)').fire('click');
    await settle(10);
    assert.deepEqual(s.calls().find((c) => c.name === 'vcs_restore').arguments.files, [path]);
  }
});
