const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle } = require('./studio-host.cjs');

const listing = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: ['scenes/main.scene.json'] }] } };
const files = { structuredContent: { root: '/g/pip', game: 'Pip', files: [
  { path: '.jm.json', size: 300 }, { path: 'assets/scripts/player.ts', size: 2048, status: 'M' },
  { path: 'assets/textures/pip.png', size: 900 }, { path: 'scenes/main.scene.json', size: 120 }, { path: 'scenes/old.scene.json', size: 0, status: 'D' }] } };
const read = { 'scenes/main.scene.json': { structuredContent: { path: 'scenes/main.scene.json', text: '{\n  "entities": []\n}\n', size: 20, etag: 'e1',
  problems: ['scenes/main.scene.json: entities[0]: unknown component "Sprite" (line 2)'] } },
  'assets/scripts/player.ts': { structuredContent: { path: 'assets/scripts/player.ts', text: 'a\nb\nc\n', size: 6, etag: 'e2' } } };
const answers = (extra = {}) => ({ files, file_read: ({ path }) => read[path], ...extra });
const open = async (opts = {}) => {
  const s = await studio(listing, { answers: answers(opts.answers), context: opts.context });
  s.tab('Files').fire('click');
  await settle(6);
  return s;
};
const row = (s, id) => s.find((e) => e.attributes['data-key'] === 'tree:' + id)[0];

test('the files are a tree with git marks; folders with changes start open', async () => {
  const s = await open();
  assert.match(s.app.text, /5 files · 2 changed since the last commit/);
  assert.ok(row(s, 'assets/scripts/player.ts'), 'a changed file shows');
  assert.equal(row(s, 'assets/textures/pip.png'), undefined, 'an unchanged folder starts folded');
  assert.match(row(s, 'assets/scripts/player.ts').text, /changed · 2\.0 KB/);
  assert.match(row(s, 'scenes/').text, /1 changed/);
  assert.match(row(s, 'scenes/old.scene.json').text, /deleted/);
});

test('a folder opens and closes from its name too', async () => {
  const s = await open();
  assert.equal(row(s, 'assets/textures/pip.png'), undefined);
  row(s, 'assets/textures/').fire('click');
  assert.ok(row(s, 'assets/textures/pip.png'));
  row(s, 'assets/textures/').fire('click');
  assert.equal(row(s, 'assets/textures/pip.png'), undefined);
});

test('a file picked is looked at: its problems and its lines; it is the model\'s context', async () => {
  const s = await open();
  row(s, 'scenes/main.scene.json').fire('click');
  await settle(6);
  assert.match(s.app.text, /unknown component "Sprite"/);
  assert.match(s.app.text, /2 {3}"entities": \[\]\n 3 }\n$/);
  assert.equal(s.contexts().at(-1).structuredContent.file, 'scenes/main.scene.json');
  s.button('Code editor').fire('click');
  await settle(6);
  assert.deepEqual(s.calls().find((c) => c.name === 'file_open'), { name: 'file_open', arguments: { path: 'scenes/main.scene.json', game: '/g/pip' } });
});

test('search finds names and lines; a hit opens the file at its line', async () => {
  const s = await open({ answers: { files_search: { structuredContent: { hits: [{ path: 'assets/scripts/player.ts', line: 2, text: 'b' }] } } } });
  const box = s.find((e) => e.attributes['data-key'] === 'files-search')[0];
  box.fire('keydown', { key: 'Enter', target: { value: 'b' } });
  await settle(6);
  s.find((e) => e.attributes['data-key'] === 'hit:0')[0].fire('click');
  await settle(6);
  const on = s.find((e) => (e.attributes.class || '').includes('files-line on'))[0];
  assert.match(on.text, /^2 b/);
});

test('rename, delete (asked twice) and new file go through the tools', async () => {
  const s = await open();
  row(s, 'assets/scripts/player.ts').fire('click');
  await settle(6);
  s.button('Rename').fire('click');
  s.find((e) => e.attributes['data-key'] === 'files-draft')[0].fire('input', { target: { value: 'assets/scripts/hero.ts' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'file_move').arguments, { from: 'assets/scripts/player.ts', to: 'assets/scripts/hero.ts', game: '/g/pip' });
  row(s, 'scenes/main.scene.json').fire('click');
  await settle(6);
  s.button('Delete').fire('click');
  assert.equal(s.calls().filter((c) => c.name === 'file_delete').length, 0);
  s.button('Delete main.scene.json?').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'file_delete').arguments, { path: 'scenes/main.scene.json', game: '/g/pip' });
  s.button('New file').fire('click');
  s.find((e) => e.attributes['data-key'] === 'files-draft')[0].fire('input', { target: { value: 'assets/data/items.json' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'file_write').arguments, { path: 'assets/data/items.json', text: '{}', etag: '', game: '/g/pip' });
});

test('the host\'s file viewer opens the file where it can', async () => {
  const s = await open();
  row(s, 'scenes/main.scene.json').fire('click');
  await settle(6);
  assert.equal(s.button('File viewer'), undefined, 'not without openai/files');
});

test('a deep link opens a file at a line', async () => {
  const s = await studio(listing, { answers: answers(), context: { 'openai/deepLink': { url: '/files?file=assets/scripts/player.ts&line=3' } } });
  await settle(8);
  assert.equal(s.current(), 'Files');
  assert.match(s.find((e) => (e.attributes.class || '').includes('files-line on'))[0].text, /^3 c/);
});

test('another game starts the tab over; a file and a folder of one name both show', async () => {
  let open = '/g/pip';
  const games = () => ({ structuredContent: { open, games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }, { name: 'Bo', path: '/g/bo', opened: 1, scenes: [] }] } });
  const both = { structuredContent: { root: '/g/bo', files: [{ path: 'foo', size: 0, status: 'D' }, { path: 'foo/bar.txt', size: 3, status: '?' }] } };
  const s = await studio(games(), { answers: { games, files: () => (open === '/g/pip' ? files : both), file_read: ({ path }) => read[path] } });
  s.tab('Files').fire('click');
  await settle(6);
  row(s, 'scenes/main.scene.json').fire('click');
  await settle(6);
  open = '/g/bo';
  s.tab('Games').fire('click');
  s.find((e) => e.attributes['data-key'] === 'card:/g/bo')[0].fire('click');
  s.button('Open').fire('click');
  await settle(10);
  s.tab('Files').fire('click');
  await settle(8);
  assert.doesNotMatch(s.app.text, /main\.scene\.json/);
  assert.ok(row(s, 'foo') && row(s, 'foo/bar.txt'));
});

test('a delete finished after another game opened leaves that game\'s tab alone', async () => {
  let open = '/g/pip', deleted;
  const games = () => ({ structuredContent: { open, games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }, { name: 'Bo', path: '/g/bo', opened: 1, scenes: [] }] } });
  const bo = { structuredContent: { root: '/g/bo', files: [{ path: 'bo.txt', size: 3 }] } };
  const s = await studio(games(), { answers: { games, files: (a) => (a.game === '/g/bo' ? bo : files), file_read: ({ path }) => read[path],
    file_delete: () => new Promise((resolve) => { deleted = () => resolve({ content: [{ type: 'text', text: 'deleted' }] }); }) } });
  s.tab('Files').fire('click');
  await settle(6);
  row(s, 'scenes/main.scene.json').fire('click');
  await settle(6);
  s.button('Delete').fire('click');
  s.button('Delete main.scene.json?').fire('click');
  await settle(4);
  open = '/g/bo';
  await s.notify('ui/notifications/tool-result', games());  // the agent opened Bo
  await settle(8);
  deleted();
  await settle(12);
  assert.equal(s.calls().filter((c) => c.name === 'files').at(-1).arguments.game, '/g/bo', 'nothing of Pip asked after Bo opened');
  assert.ok(row(s, 'bo.txt'), 'Bo\'s files show');
});
