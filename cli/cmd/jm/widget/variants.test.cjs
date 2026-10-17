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
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_compare').arguments, { a: 'floaty', frame: 30, game: '/g/pip' });
  assert.deepEqual(s.find((e) => e.tag === 'img' && e.attributes.alt && e.attributes.alt.includes('frame 30')).map((e) => e.attributes.src),
    ['data:image/jpeg;base64,a', 'data:image/jpeg;base64,b']);
  assert.match(s.app.text, /changed Player: VelocityComponent/);
});

test('merge and drop ask first; a new variant is made from its name', async () => {
  const s = await open();
  s.button('Merge').fire('click');
  s.button('Merge into the game?').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_merge').arguments, { name: 'floaty', game: '/g/pip' });
  s.button('Drop').fire('click');
  assert.ok(s.button('Drop? (1 uncommitted files go)'));
  key(s, 'var-new').fire('input', { target: { value: 'big-player' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_new').arguments, { name: 'big-player', game: '/g/pip' });
});

test('a sandbox is kept as a variant', async () => {
  const s = await open({ 'openai/deepLink': { url: '/variants?sandbox=sb1' } });
  assert.match(s.app.text, /scenes\/main\.scene\.json\s+changed/);
  s.button('Keep as a variant').fire('click');
  key(s, 'var-keep').fire('input', { target: { value: 'kept' } });
  s.find((e) => e.tag === 'form').at(-1).fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'sandbox_promote').arguments, { name: 'sb1', variant: 'kept', game: '/g/pip' });
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

test('another game opened meanwhile: a drop, merge or keep acts on the game shown, and leaves the other alone', async () => {
  for (const [tool, k, confirm] of [['variant_drop', 'confirm:drop:floaty', true], ['variant_merge', 'confirm:merge:floaty', true], ['sandbox_drop', 'confirm:sdrop:sb1', true]]) {
    let open = '/g/pip', finish;
    const games = () => ({ structuredContent: { open, games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }, { name: 'Bo', path: '/g/bo', opened: 1, scenes: [] }] } });
    const s = await studio(games(), { answers: { ...answers, games, [tool]: () => new Promise((resolve) => { finish = () => resolve({ content: [{ type: 'text', text: 'done' }] }); }) } });
    s.tab('Variants').fire('click');
    await settle(8);
    if (tool === 'sandbox_drop') { key(s, 'sandbox:sb1').fire('click'); await settle(2); }
    key(s, k).fire('click');
    if (confirm) key(s, k).fire('click');
    await settle(4);
    open = '/g/bo';
    await s.notify('ui/notifications/tool-result', games());  // the agent opened Bo
    await settle(8);
    finish();
    await settle(12);
    assert.equal(s.calls().find((c) => c.name === tool).arguments.game, '/g/pip', tool);
    const lists = s.calls().filter((c) => c.name === 'variants' || c.name === 'sandboxes');
    assert.ok(lists.every((c) => c.arguments.game), 'every list names its game');
    assert.equal(lists.at(-1).arguments.game, '/g/bo', `${tool}: nothing of Pip asked after Bo opened`);
  }
});

test('a list that fails says so, with its data from before; a real empty list is empty', async () => {
  const fail = { variants: [{ isError: true, content: [{ type: 'text', text: 'variants broke' }] }, { rpcError: 'variants gone' }],
                 sandboxes: [{ isError: true, content: [{ type: 'text', text: 'sandboxes broke' }] }, { rpcError: 'sandboxes gone' }] };
  for (const side of ['variants', 'sandboxes']) {
    for (const failure of fail[side]) {
      let now = 'ok';
      const answer = (ok) => () => (now === 'fail' ? failure : now === 'empty' ? { structuredContent: { [side]: [] } } : ok);
      const s = await studio(listing, { answers: { ...answers, [side]: answer(answers[side]) } });
      s.tab('Variants').fire('click');
      await settle(8);
      now = 'fail';
      s.tab('Games').fire('click');  // a visit asks again
      s.tab('Variants').fire('click');
      await settle(8);
      const text = failure.rpcError || failure.content[0].text;
      assert.match(s.app.text, new RegExp(text), `${side}: the failure is said`);
      assert.match(s.app.text, /may be out of date/);
      assert.match(s.app.text, side === 'variants' ? /floaty/ : /sb1/, 'the data from before stays');
      assert.match(s.app.text, side === 'variants' ? /sb1/ : /floaty/, 'the other list shows');
      now = 'empty';
      s.button('Try again').fire('click');
      await settle(8);
      assert.doesNotMatch(s.app.text, new RegExp(text));
      if (side === 'variants') assert.match(s.app.text, /None yet/);
      else assert.doesNotMatch(s.app.text, /Sandboxes ·/);
    }
  }
});

test('a first list that fails shows its error beside the other list, or both errors', async () => {
  const broke = (what) => () => ({ isError: true, content: [{ type: 'text', text: `${what} broke` }] });
  let s = await studio(listing, { answers: { ...answers, variants: broke('variants') } });
  s.tab('Variants').fire('click');
  await settle(8);
  assert.match(s.app.text, /variants broke/);
  assert.match(s.app.text, /sb1/, 'the sandboxes show');
  s = await studio(listing, { answers: { ...answers, variants: broke('variants'), sandboxes: broke('sandboxes') } });
  s.tab('Variants').fire('click');
  await settle(8);
  assert.match(s.app.text, /variants broke[\s\S]*sandboxes broke/);
});
