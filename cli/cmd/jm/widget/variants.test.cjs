const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle } = require('./studio-host.cjs');

const listing = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }] } };
const variants = { structuredContent: { variants: [{ name: 'floaty', dir: '/g/pip/.jm/variants/floaty', branch: 'variant/floaty', ahead: 2, behind: 0, changed: 1, subject: 'Floatier jump' }] },
  _meta: { 'jm/thumbs': { floaty: 'data:image/jpeg;base64,f' } } };
const sandboxes = { structuredContent: { sandboxes: [{ name: 'sb1', dir: '/g/pip/.jm/sandboxes/sb1', made: 1, expires: 1792200000, changes: { 'scenes/main.scene.json': 'M' } }] },
  _meta: { 'jm/thumbs': { sb1: 'data:image/jpeg;base64,s' } } };
const compared = { structuredContent: { compared: { a: 'floaty', b: 'game', frame: 30, errors: ['', ''], uncommitted: [0, 3],
  changes: [{ path: 'scenes/main.scene.json', status: 'M', entities: [{ entity: 'Player', change: 'changed', changed: ['VelocityComponent'] }] }] } },
  _meta: { 'jm/frames': ['data:image/jpeg;base64,a', 'data:image/jpeg;base64,b'] } };
const answers = { variants, sandboxes, variant_compare: compared, plays_list: { structuredContent: { plays: [] } } };
const open = async (context, more = {}) => {
  const s = await studio(listing, { answers: { ...answers, ...more }, context });
  if (!context) s.tab('Variants').fire('click');
  await settle(8);
  return s;
};
const open_ = (more) => open(undefined, more);
const key = (s, k) => s.find((e) => e.attributes['data-key'] === k)[0];

test('variants and sandboxes are listed; the first variant is shown and is the context', async () => {
  const s = await open();
  assert.match(s.app.text, /floaty\s+2 ahead · 1 uncommitted/);
  assert.match(s.app.text, /sb1\s+1 file differs/);
  assert.ok(s.find((e) => e.tag === 'img' && e.attributes.src === 'data:image/jpeg;base64,s').length, "a sandbox's picture");
  assert.equal(s.contexts().at(-1).structuredContent.variant, 'floaty');
});

test('compare shows both frames side by side and what differs', async () => {
  const s = await open();
  key(s, 'var-frame').fire('input', { target: { value: '30' } });
  s.find((e) => e.tag === 'form').at(-1).fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_compare').arguments, { a: 'floaty', play: 'none', at: '30', game: '/g/pip' }, 'no plays: at a frame');
  assert.deepEqual(s.find((e) => e.tag === 'img' && e.attributes.alt && e.attributes.alt.includes('frame 30')).map((e) => e.attributes.src),
    ['data:image/jpeg;base64,a', 'data:image/jpeg;base64,b']);
  assert.match(s.app.text, /changed Player: VelocityComponent/);
  assert.match(s.app.text, /the game has 3 uncommitted files: its frame shows them/);
  assert.doesNotMatch(s.app.text, /floaty has \d+ uncommitted/);
});

test("compare goes to the person's last marker of their latest play, and plays the variant from there", async () => {
  const plays = { structuredContent: { plays: [
    { id: 'agent1', started: '2026-10-15T11:00:00Z', seconds: 9, by: 'agent', marks: ['m1 0:01.0'] },
    { id: 'p2', started: '2026-10-15T10:15:00Z', seconds: 75, marks: ['m1 0:03.0', 'm2 0:40.0 "too floaty"'] },
    { id: 'p1', started: '2026-10-14T10:15:00Z', seconds: 20 }] } };
  const s = await open_({ plays_list: plays, variant_compare: { ...compared, structuredContent: { compared: { ...compared.structuredContent.compared, play: 'p2', at: 'm2' } } } });
  const args = () => s.calls().filter((c) => c.name === 'variant_compare').at(-1).arguments;
  const submit = async () => { s.find((e) => e.tag === 'form').at(-1).fire('submit'); await settle(8); };
  await submit();
  assert.deepEqual(args(), { a: 'floaty', play: 'p2', at: 'm2', game: '/g/pip' }, "theirs, not the agent's newer run, at its last marker");
  assert.match(s.app.text, /floaty · marker 2 \(frame 30\)/);
  s.button('Play floaty from there').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_play').arguments, { name: 'floaty', play: 'p2', at: '30', game: '/g/pip' }, 'the frame shown');
  const [playPick] = s.find((e) => e.tag === 'select' && e.attributes['aria-label'] === 'Play');
  playPick.fire('change', { target: { value: 'p1' } });
  await submit();
  assert.deepEqual(args(), { a: 'floaty', play: 'p1', at: 'end', game: '/g/pip' }, 'a play with no markers: its end');
  s.find((e) => e.tag === 'select' && e.attributes['aria-label'] === 'Play')[0].fire('change', { target: { value: 'none' } });
  await submit();
  assert.deepEqual(args(), { a: 'floaty', play: 'none', at: '120', game: '/g/pip' }, 'no input: a frame');
});

test("while the plays are read, compare waits; if they can't be, jm picks the moment", async () => {
  let answer;
  const s = await open_({ plays_list: new Promise((resolve) => { answer = resolve; }) });
  assert.equal(s.button('Compare').attributes.disabled, '', 'not with no inputs while the plays are read');
  assert.match(s.app.text, /reading the plays/);
  answer({ isError: true, content: [{ type: 'text', text: 'no plays folder' }] });
  await settle(8);
  s.find((e) => e.tag === 'form').at(-1).fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_compare').arguments, { a: 'floaty', game: '/g/pip' }, "jm's default: not no inputs");
});

test("the agent's compare opens here: the variant, both frames at the person's marker, and Play from there", async () => {
  const theirs = { ...compared.structuredContent.compared, game: '/g/pip', a: 'floaty', play: 'p2', at: 'm2' };
  const plays = { structuredContent: { plays: [{ id: 'p2', started: '2026-10-15T10:15:00Z', seconds: 75, marks: ['m1 0:03.0', 'm2 0:40.0'] }] } };
  const s = await studio({ structuredContent: { ...listing.structuredContent, compared: theirs }, _meta: compared._meta },
    { input: { a: 'floaty', at: 'm2' }, answers: { ...answers, plays_list: plays } });
  await settle(8);
  assert.match(s.app.text, /floaty · marker 2 \(frame 30\)/, 'its comparison, shown without comparing again');
  assert.ok(!s.calls().some((c) => c.name === 'variant_compare'), 'not run again');
  assert.equal(s.find((e) => e.tag === 'select' && e.attributes['aria-label'] === 'Moment')[0].children.find((o) => o.attributes.selected !== undefined).attributes.value, 'm2', 'the form at its moment');
  s.button('Play floaty from there').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_play').arguments, { name: 'floaty', play: 'p2', at: '30', game: '/g/pip' });
  const other = await studio({ structuredContent: { ...listing.structuredContent, compared: { ...theirs, game: '/g/other' } }, _meta: compared._meta },
    { input: { a: 'floaty', at: 'm2' }, answers: { ...answers, plays_list: plays } });
  await settle(8);
  assert.doesNotMatch(other.app.text, /frame 30/, "another game's comparison isn't shown as this one's");
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

test("after an action the tab stays on its game, though the server's open one moved", async () => {
  const elsewhere = { structuredContent: { open: '/g/other', games: [...listing.structuredContent.games, { name: 'Other', path: '/g/other', opened: 2, scenes: [] }] } };
  const s = await open_({ games: elsewhere });
  key(s, 'var-new').fire('input', { target: { value: 'big-player' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(8);
  const after = s.calls().slice(s.calls().findIndex((c) => c.name === 'variant_new') + 1).filter((c) => c.name !== 'games');
  assert.ok(after.length, 'the lists are read again');
  assert.deepEqual([...new Set(after.map((c) => c.arguments.game))], ['/g/pip']);
});

test('a sandbox builds and tests in place: what jm said shows beside it, a failure marked', async () => {
  const s = await open({ 'openai/deepLink': { url: '/variants?sandbox=sb1' } },
    { sandbox_run: (args) => args.args[0] === 'build' ? { content: [{ type: 'text', text: 'built\n3 scripts, 7 scenes' }] }
      : { isError: true, content: [{ type: 'text', text: '1 test failed: jump height' }] } });
  s.button('Build').fire('click');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'sandbox_run').arguments, { name: 'sb1', args: ['build'], game: '/g/pip' });
  assert.match(s.app.text, /build passed[\s\S]*3 scripts, 7 scenes/);
  s.button('Test').fire('click');
  await settle(8);
  assert.match(s.app.text, /test failed[\s\S]*jump height/);
});

test('a sandbox is kept as a variant', async () => {
  const s = await open({ 'openai/deepLink': { url: '/variants?sandbox=sb1' } });
  assert.match(s.app.text, /scenes\/main\.scene\.json\s+changed/);
  s.button('Keep as a variant').fire('click');
  key(s, 'var-keep').fire('input', { target: { value: 'kept' } });
  s.find((e) => e.tag === 'form' && e.all((k) => k.attributes['data-key'] === 'var-keep').length)[0].fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'sandbox_promote').arguments, { name: 'sb1', variant: 'kept', game: '/g/pip' });
});

test('a sandbox is compared with the game too: both frames, and its changes per entity', async () => {
  let changes = { 'scenes/main.scene.json': 'M' };
  const sb = () => ({ structuredContent: { sandboxes: [{ ...sandboxes.structuredContent.sandboxes[0], changes }] } });
  const s = await open({ 'openai/deepLink': { url: '/variants?sandbox=sb1' } },
    { sandboxes: sb, variant_compare: { ...compared, structuredContent: { compared: { ...compared.structuredContent.compared, a: 'sb1', sandbox: true } } } });
  assert.match(s.app.text, /scenes\/main\.scene\.json\s+changed/);
  s.find((e) => e.tag === 'form').at(-1).fire('submit');
  await settle(8);
  assert.deepEqual(s.calls().find((c) => c.name === 'variant_compare').arguments, { a: 'sb1', sandbox: true, play: 'none', at: '120', game: '/g/pip' });
  assert.deepEqual(s.find((e) => e.tag === 'img' && e.attributes.alt && e.attributes.alt.includes('frame 30')).map((e) => e.attributes.alt),
    ['sb1 at frame 30', 'the game at frame 30']);
  assert.equal(s.app.text.match(/changed Player: VelocityComponent/g).length, 1);
  const rowShot = (key) => s.find((e) => e.attributes['data-key'] === key)[0].all((e) => e.tag === 'img')[0].attributes.src;
  assert.equal(rowShot('sandbox:sb1'), 'data:image/jpeg;base64,a', 'its row shows the frame');
  // Another compared then: each row keeps its own picture.
  s.find((e) => e.attributes['data-key'] === 'variant:floaty')[0].fire('click');
  await settle(8);
  s.find((e) => e.tag === 'form').at(-1).fire('submit');
  await settle(8);
  assert.deepEqual([rowShot('variant:floaty'), rowShot('sandbox:sb1')], ['data:image/jpeg;base64,a', 'data:image/jpeg;base64,a'], 'both rows show their frames');
  s.find((e) => e.attributes['data-key'] === 'sandbox:sb1')[0].fire('click');
  await settle(8);
  // Changed since: the list says so, not what Compare saw.
  changes = { 'scenes/main.scene.json': 'M', 'new.ts': 'A' };
  s.tab('Games').fire('click');
  s.tab('Variants').fire('click');
  await settle(8);
  assert.match(s.app.text, /new\.ts\s+added/);
  assert.doesNotMatch(s.app.text, /VelocityComponent/);
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

test('work in a variant: every tab says so, the games fold it into its game, and Back returns', async () => {
  let open = '/g/pip';
  const dir = '/g/pip/.jm/variants/floaty';
  const games = () => ({ structuredContent: { open, games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] },
    { name: 'Pip', path: dir, opened: 2, scenes: [], copyOf: '/g/pip', copyKind: 'variant', copy: 'floaty' }] } });
  const s = await open_({ games, open_game: (a) => { open = a.game; return { content: [{ type: 'text', text: 'Opened.' }] }; } });
  s.button('Work here').fire('click');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'open_game').arguments, { game: dir });
  assert.match(s.app.text, /Working in variant\s+floaty\s*: changes go here, not to the game\./);
  assert.ok(s.contexts().at(-1).structuredContent.copyOf === '/g/pip');
  s.tab('Variants').fire('click');
  await settle(8);
  assert.match(s.app.text, /This is the variant floaty/);
  s.tab('Games').fire('click');
  await settle(8);
  assert.match(s.app.text, /Your games 1/);
  assert.match(s.app.text, /in floaty/);
  s.button('Back to the game').fire('click');
  await settle(10);
  assert.equal(s.calls().filter((c) => c.name === 'open_game').at(-1).arguments.game, '/g/pip');
  assert.doesNotMatch(s.app.text, /Working in variant/);
});
