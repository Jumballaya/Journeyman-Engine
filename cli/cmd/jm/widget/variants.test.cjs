const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle } = require('./studio-host.cjs');

const listing = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }] } };
const variants = { structuredContent: { variants: [{ name: 'floaty', dir: '/g/pip/.jm/variants/floaty', branch: 'variant/floaty', ahead: 2, behind: 0, changed: 1, subject: 'Floatier jump' }] },
  _meta: { 'jm/thumbs': { floaty: 'data:image/jpeg;base64,f' } } };
const sandboxes = { structuredContent: { sandboxes: [{ name: 'sb1', dir: '/g/pip/.jm/sandboxes/sb1', made: 1, expires: 1792200000, changes: { 'scenes/main.scene.json': 'M' } }] } };
const compared = { structuredContent: { a: 'floaty', b: 'game', frame: 30, errors: ['', ''],
  changes: [{ path: 'scenes/main.scene.json', status: 'M', entities: [{ entity: 'Player', change: 'changed', changed: ['VelocityComponent'] }] }] },
  _meta: { 'jm/frames': ['data:image/jpeg;base64,a', 'data:image/jpeg;base64,b'] } };
const answers = { variants, sandboxes, variant_compare: compared };
const open = async (context) => {
  const s = await studio(listing, { answers, context });
  if (!context) s.tab('Variants').fire('click');
  await settle(8);
  return s;
};
const key = (s, k) => s.find((e) => e.attributes['data-key'] === k)[0];

test('variants and sandboxes are listed; the first variant is shown and is the context', async () => {
  const s = await open();
  assert.match(s.app.text, /floaty\s+2 ahead · 1 uncommitted/);
  assert.match(s.app.text, /sb1\s+1 files differ/);
  assert.equal(s.contexts().at(-1).structuredContent.variant, 'floaty');
});

test('compare shows both frames side by side and what differs', async () => {
  const s = await open();
  key(s, 'var-frame').fire('input', { target: { value: '30' } });
  s.find((e) => e.tag === 'form').at(-1).fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_compare').arguments, { a: 'floaty', frame: 30 });
  assert.deepEqual(s.find((e) => e.tag === 'img' && e.attributes.alt && e.attributes.alt.includes('frame 30')).map((e) => e.attributes.src),
    ['data:image/jpeg;base64,a', 'data:image/jpeg;base64,b']);
  assert.match(s.app.text, /changed Player: VelocityComponent/);
});

test('merge and drop ask first; a new variant is made from its name', async () => {
  const s = await open();
  s.button('Merge').fire('click');
  s.button('Merge into the game?').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_merge').arguments, { name: 'floaty' });
  s.button('Drop').fire('click');
  assert.ok(s.button('Drop? (1 uncommitted files go)'));
  key(s, 'var-new').fire('input', { target: { value: 'big-player' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_new').arguments, { name: 'big-player' });
});

test('a sandbox is kept as a variant', async () => {
  const s = await open({ 'openai/deepLink': { url: '/variants?sandbox=sb1' } });
  assert.match(s.app.text, /scenes\/main\.scene\.json\s+changed/);
  s.button('Keep as a variant').fire('click');
  key(s, 'var-keep').fire('input', { target: { value: 'kept' } });
  s.find((e) => e.tag === 'form').at(-1).fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'sandbox_promote').arguments, { name: 'sb1', variant: 'kept' });
});

test('an action keeps the pick the person made after a deep link', async () => {
  const two = { structuredContent: { variants: [variants.structuredContent.variants[0], { ...variants.structuredContent.variants[0], name: 'beta' }] } };
  const s = await studio(listing, { answers: { ...answers, variants: two }, context: { 'openai/deepLink': { url: '/variants?variant=floaty' } } });
  await settle(8);
  key(s, 'variant:beta').fire('click');
  s.button('Play').fire('click');
  await settle(8);
  assert.equal(s.contexts().at(-1).structuredContent.variant, 'beta');
});
