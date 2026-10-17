// ChatGPT's window.openai extensions in Studio: uploads into the game's
// assets, and destructive actions confirmed in the host's modal.
const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle, plain } = require('./studio-host.cjs');

const listing = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }] } };
const files = { structuredContent: { root: '/g/pip', game: 'Pip', files: [{ path: 'scenes/main.scene.json', size: 120 }] } };
const status = { structuredContent: { changes: [{ path: 'assets/scripts/bat.ts', status: '?' }] } };
const log = { structuredContent: { versions: [{ hash: 'a'.repeat(40), subject: 'New game', author: 'Pat', when: 1791000000, files: {} }] } };
const answers = { files, vcs_status: status, vcs_log: log, vcs_diff: { structuredContent: { file: 'assets/scripts/bat.ts', diff: '' } },
  file_read: { structuredContent: { path: 'scenes/main.scene.json', text: '{}', size: 2, etag: 'e' } } };
const open = async (tab, openai) => {
  const s = await studio(listing, { answers, openai });
  s.tab(tab).fire('click');
  await settle(8);
  return s;
};

test('Add file shows only where the host takes uploads; an upload goes to asset_add', async () => {
  assert.equal((await open('Files')).button('Add file'), undefined);
  const uploaded = [];
  const s = await open('Files', {
    uploadFile: async (file) => { uploaded.push(file.name); return { fileId: 'file_9' }; },
    getFileDownloadUrl: async ({ fileId }) => ({ downloadUrl: 'https://files.example/' + fileId }),
  });
  assert.ok(s.button('Add file'));
  const input = s.find((e) => e.attributes['data-key'] === 'files-add')[0];
  input.fire('change', { target: { files: [{ name: 'hero.png', type: 'image/png' }], value: 'C:\\\\hero.png' } });
  await settle(10);
  assert.deepEqual(uploaded, ['hero.png']);
  assert.deepEqual(s.calls().find((c) => c.name === 'asset_add').arguments, { game: '/g/pip',
    file: { download_url: 'https://files.example/file_9', file_id: 'file_9', file_name: 'hero.png', mime_type: 'image/png' } });
});

test('with a host modal, discard asks there, runs nothing here, and reads the history again after', async () => {
  const asked = [];
  const s = await open('Git', { requestModal: async (m) => { asked.push(m); }, requestClose() {} });
  const before = s.calls().filter((c) => c.name === 'vcs_status').length;
  s.button('Discard').fire('click');
  await settle(10);
  assert.equal(s.calls().filter((c) => c.name === 'vcs_restore').length, 0);
  assert.deepEqual(plain(asked), [{ params: { confirm: { label: 'Discard', ask: 'Discard? (kept in the stash)', tool: 'vcs_restore',
    args: { files: ['assets/scripts/bat.ts'], game: '/g/pip' } } } }]);
  assert.ok(s.calls().filter((c) => c.name === 'vcs_status').length > before, 'read again');
  assert.ok(s.button('Discard'), 'not armed here');
});

test('a host that refuses the modal gets the confirm here instead', async () => {
  const s = await open('Git', { requestModal: async () => { throw new Error('no'); }, requestClose() {} });
  s.button('Discard').fire('click');
  await settle(6);
  s.button('Discard? (kept in the stash)').fire('click');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'vcs_restore').arguments, { files: ['assets/scripts/bat.ts'], game: '/g/pip' });
});

test('back from a modal the host closed, the page reads again (once)', async () => {
  let resolve;
  const s = await open('Git', { requestModal: () => new Promise((r) => { resolve = r; }), requestClose() {} });
  s.button('Discard').fire('click');
  const count = () => s.calls().filter((c) => c.name === 'vcs_status').length, before = count();
  await s.windowEvent('focus');
  await settle(8);
  assert.ok(count() > before);
  const after = count();
  await s.windowEvent('focus');
  await settle(8);
  assert.equal(count(), after, 'a later focus is just a focus');
  resolve();
});

test('as the modal, the page asks, runs the tool on yes and closes; anything else confirms nothing', async () => {
  let closed = 0;
  const confirm = { label: 'Delete', ask: 'Delete main.scene.json?', tool: 'file_delete', args: { path: 'scenes/main.scene.json', game: '/g/pip' } };
  const s = await studio(listing, { answers, openai: { view: { mode: 'modal', params: { confirm } }, requestModal() {}, requestClose() { closed++; } } });
  assert.match(s.app.text, /Delete main\.scene\.json\?/);
  s.button('Delete').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().map((c) => c.name).filter((n) => n !== 'games'), ['file_delete']);
  assert.deepEqual(s.calls().find((c) => c.name === 'file_delete').arguments, confirm.args);
  assert.equal(closed, 1);

  const failing = await studio(listing, { answers: { ...answers, file_delete: { isError: true, content: [{ type: 'text', text: 'locked' }] } },
    openai: { view: { mode: 'modal', params: { confirm } }, requestModal() {}, requestClose() { closed++; } } });
  failing.button('Delete').fire('click');
  await settle(8);
  assert.match(failing.app.text, /Couldn't: locked/);
  assert.equal(closed, 1, 'left open to say why');

  const other = await studio(listing, { answers, openai: { view: { mode: 'modal', params: { confirm: { ...confirm, tool: 'file_write' } } }, requestModal() {}, requestClose() {} } });
  assert.match(other.app.text, /Nothing to confirm/);
  assert.equal(other.calls().filter((c) => c.name === 'file_write').length, 0);
});

test('an upload finished after another game opened still goes to the game it was for; the other\'s Files reads its own', async () => {
  let finish, now = listing;
  const other = { structuredContent: { open: '/g/other', games: [...listing.structuredContent.games, { name: 'Other', path: '/g/other', opened: 2, scenes: [] }] } };
  const s = await studio(listing, { answers: { ...answers, games: () => now },
    openai: { uploadFile: () => new Promise((r) => { finish = r; }), getFileDownloadUrl: async ({ fileId }) => ({ downloadUrl: 'https://f.example/' + fileId }) } });
  s.tab('Files').fire('click');
  await settle(8);
  s.find((e) => e.attributes['data-key'] === 'files-add')[0].fire('change', { target: { files: [{ name: 'hero.png', type: 'image/png' }] } });
  now = other;
  await s.notify('ui/notifications/tool-result', other);
  await settle(8);
  finish({ fileId: 'file_1' });
  await settle(12);
  assert.equal(s.calls().find((c) => c.name === 'asset_add').arguments.game, '/g/pip');
  assert.match(s.app.text, /Other/);
  assert.doesNotMatch(s.app.text, /Reading the files/);
  assert.equal(s.calls().filter((c) => c.name === 'file_read').length, 0, 'the other game\'s tab picks nothing of Pip\'s');
});

test('a merge confirmed in the modal clears the comparison shown', async () => {
  const variants = { structuredContent: { variants: [{ name: 'floaty', dir: '/d', branch: 'variant/floaty', ahead: 1, behind: 0, changed: 0, subject: 'x' }] } };
  const compared = { structuredContent: { a: 'floaty', b: 'game', frame: 120, errors: ['', ''], changes: [] }, _meta: { 'jm/frames': ['data:image/jpeg;base64,a', 'data:image/jpeg;base64,b'] } };
  const s = await studio(listing, { answers: { ...answers, variants, sandboxes: { structuredContent: { sandboxes: [] } }, variant_compare: compared },
    openai: { requestModal: async () => {}, requestClose() {} } });
  s.tab('Variants').fire('click');
  await settle(8);
  s.find((e) => e.tag === 'form').at(-1).fire('submit');
  await settle(8);
  assert.match(s.app.text, /floaty · frame 120/);
  s.button('Merge').fire('click');
  await settle(10);
  assert.doesNotMatch(s.app.text, /floaty · frame 120/);
  assert.equal(s.calls().filter((c) => c.name === 'variant_merge').length, 0, 'the modal runs it');
});
