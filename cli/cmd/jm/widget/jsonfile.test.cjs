const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
// kit.jsonFile over a host whose reads and writes finish when the test says.
const { kit } = vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'kit.js'), 'utf8') + '; ({ kit })', {});
const text = (doc) => kit.jsonText(doc) + '\n';
function fakeHost() {
  const pending = [];
  const later = (kind, args) => new Promise((resolve, reject) => pending.push({ kind, args, resolve, reject }));
  return { pending, readResource: (uri) => later('read', [uri]), writeResource: (...args) => later('write', args) };
}
const settle = () => new Promise((resolve) => setImmediate(resolve));
function opened(host) {
  const said = [];
  const f = kit.jsonFile(host, { say: (n) => said.push(n), changed() {} });
  return { f, said };
}

test('a read that ends after a newer one began changes nothing', async () => {
  const host = fakeHost(), { f } = opened(host);
  f.open('a'); f.open('b');
  const [a, b] = host.pending;
  b.resolve({ text: text({ of: 'b' }), etag: 'b1', writable: true }); await settle();
  a.resolve({ text: text({ of: 'a' }), etag: 'a1', writable: true }); await settle();
  assert.equal(f.file.uri, 'b');
  assert.equal(f.file.doc.of, 'b');
  assert.ok(f.editable('b') && !f.editable('a'));
});

test('a file is read once, and writable only as jm fmt writes it, with a version', async () => {
  for (const [r, writable] of [[{ text: text({ x: [1, 2] }), etag: 'v1', writable: true }, true],
                               [{ text: '{"x":[1,2]}\n', etag: 'v1', writable: true }, false],
                               [{ text: text({ x: 1 }), writable: true }, false]]) {
    const host = fakeHost(), { f } = opened(host);
    f.open('a'); f.open('a');
    assert.equal(host.pending.length, 1);
    host.pending[0].resolve(r); await settle();
    assert.equal(f.editable('a'), writable);
  }
});

test('one save at a time; a conflict reads the file again', async () => {
  const host = fakeHost(), { f, said } = opened(host);
  f.open('a');
  host.pending[0].resolve({ text: text({ n: 1 }), etag: 'v1', writable: true }); await settle();
  const first = f.save((d) => { d.n = 2; });
  assert.equal(await f.save((d) => { d.n = 3; }), null, 'a save under way');
  const write = host.pending[1];
  assert.deepEqual([write.args[1], write.args[2]], [text({ n: 2 }), 'v1']);
  write.resolve({ outcome: 'conflict' }); await settle();
  host.pending[2].resolve({ text: text({ n: 7 }), etag: 'v2', writable: true });
  assert.equal(await first, 'conflict');
  assert.deepEqual([f.file.doc.n, f.file.etag], [7, 'v2']);
  assert.match(said.at(-1), /changed since/);
});

test('a save that ends after another file opened changes nothing shown', async () => {
  const host = fakeHost(), { f, said } = opened(host);
  f.open('a');
  host.pending[0].resolve({ text: text({ n: 1 }), etag: 'v1', writable: true }); await settle();
  const saving = f.save((d) => { d.n = 2; });
  f.open('b');
  host.pending[2].resolve({ text: text({ of: 'b' }), etag: 'b1', writable: true }); await settle();
  host.pending[1].resolve({ outcome: 'saved', etag: 'v2' });
  assert.equal(await saving, null);
  assert.equal(f.file.uri, 'b');
  assert.equal(said.at(-1), 'Saving…', 'nothing said of a file no longer shown');
});

test('a save too large, or failing, says so', async () => {
  for (const [answer, said] of [[{ outcome: 'too-large', maxBytes: 10 }, 'Too large to save (at most 10 bytes).'], [{}, "Couldn't save."]]) {
    const host = fakeHost(), o = opened(host);
    o.f.open('a');
    host.pending[0].resolve({ text: text({ n: 1 }), etag: 'v1', writable: true }); await settle();
    const saving = o.f.save((d) => { d.n = 2; });
    host.pending[1].resolve(answer);
    assert.equal(await saving, 'failed');
    assert.equal(o.said.at(-1), said);
    assert.equal(o.f.file.doc.n, 1);
  }
});
