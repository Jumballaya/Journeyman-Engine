const assert = require('node:assert/strict');
const { test } = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const { kit } = vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'kit.js'), 'utf8') + '; ({ kit })', { Promise });

test('kit.latest answers its newest call only, and none once its owner changes', async () => {
  let owner = 'a';
  const asking = kit.latest(() => owner);
  const done = [];
  const first = asking(() => new Promise((resolve) => done.push(resolve)));
  const second = asking(() => new Promise((resolve) => done.push(resolve)));
  await null;
  done[1]({ content: [{ type: 'text', text: 'second' }] });
  done[0]({ content: [{ type: 'text', text: 'first' }] });
  assert.equal(await first, null);
  assert.equal(kit.said(await second), 'second');

  const third = asking(() => Promise.resolve({ content: [] }));
  owner = 'b';
  assert.equal(await third, null, 'asked for another owner');
});

test('kit.answer makes a failed call an error answer', async () => {
  const r = await kit.answer(Promise.reject(new Error('no host')));
  assert.equal(r.isError, true);
  assert.equal(kit.said(r), 'no host');
  assert.equal(kit.said(await kit.latest(() => 1)(() => { throw new Error('thrown'); })), 'thrown');
});
