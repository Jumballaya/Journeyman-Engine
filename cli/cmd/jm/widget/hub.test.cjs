const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
// The page as jm serves it: the bridge inlined, then the hub's own script.
const html = fs.readFileSync(path.join(__dirname, 'hub.html'), 'utf8')
  .replace('/*__JM_BRIDGE__*/', fs.readFileSync(path.join(__dirname, 'bridge.js'), 'utf8'));
const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]).join('\n');

class Element {
  constructor(tag) { Object.assign(this, { tag, children: [], attributes: {}, listeners: {}, dataset: {}, nodeType: 1 }); }
  append(...kids) { this.children.push(...kids); }
  replaceChildren() { this.children = []; }
  setAttribute(k, v) { this.attributes[k] = v; }
  addEventListener(name, fn) { this.listeners[name] = fn; }
  focus() { focused.at = this; }
  get text() { return this.children.map((c) => (typeof c === 'string' ? c : c.text)).join(' '); }
  all(pred) { return [...(pred(this) ? [this] : []), ...this.children.flatMap((c) => (c.nodeType ? c.all(pred) : []))]; }
}
const focused = { at: null };
const settle = () => new Promise((resolve) => setImmediate(resolve));
const plain = (v) => JSON.parse(JSON.stringify(v));

// The hub in an MCP Apps host that answers tools from answers (name → result).
async function hub(result, answers = {}, { delayInit = false, holdContext = false } = {}) {
  let fromInit = () => {};
  const app = new Element('app');
  const listeners = {};
  const sent = [];
  const held = [];  // context acknowledgements, when the host holds them
  const parent = {
    postMessage(m) {
      sent.push(m);
      const reply = (r) => setImmediate(() => listeners.message({ source: parent, data: { jsonrpc: '2.0', id: m.id, result: r } }));
      if (m.method === 'ui/initialize') {
        const answer = () => reply({ hostCapabilities: { updateModelContext: { text: {} } } });
        if (delayInit) fromInit = answer; else answer();
      }
      else if (m.method === 'tools/call') reply(answers[m.params.name] || (m.params.name === 'games' ? games : { content: [{ type: 'text', text: 'ok' }] }));
      else if (holdContext && m.method === 'ui/update-model-context') held.push(() => reply({}));
      else if (m.id !== undefined) reply({});
    },
  };
  const window = { parent, addEventListener(name, fn) { listeners[name] = fn; } };
  vm.runInNewContext(script, {
    window, setTimeout, Date,
    document: { createElement: (tag) => new Element(tag), getElementById: () => app,
                querySelector: () => app.all((e) => e.attributes['aria-pressed'] === 'true')[0], documentElement: new Element('html'), body: { scrollHeight: 500 } },
    matchMedia: () => ({ matches: false, addEventListener() {} }), requestAnimationFrame() {},
  });
  await settle();
  listeners.message({ source: parent, data: { jsonrpc: '2.0', method: 'ui/notifications/tool-result', params: result } });
  const find = (pred) => app.all(pred);
  // A game's card's pick button.
  const pick = (path) => find((e) => e.tag === 'div' && e.attributes.title === path)[0].children[0];
  if (delayInit) return { app, sent, find, pick, fromInit: () => fromInit() };
  const button = (label) => find((e) => e.tag === 'button' && e.text === label)[0];
  const calls = () => sent.filter((m) => m.method === 'tools/call').map((m) => plain(m.params));
  return { app, sent, find, pick, button, calls, held };
}

const games = {
  content: [{ type: 'text', text: 'Games' }],
  structuredContent: { open: '/g/neon', games: [
    { name: 'Neon Vow', path: '/g/neon', opened: Math.floor(Date.now() / 1000) - 60, scenes: ['a', 'b'] },
    { name: 'Tetris', path: '/g/tetris', opened: 0, scenes: ['a'] },
    { name: 'Gone', path: '/g/gone', opened: 5, scenes: [], missing: true }] },
  _meta: { 'jm/thumbs': { '/g/neon': 'data:image/jpeg;base64,neon' } },
};

test('the games show as cards: picture, when opened, which is open here, which went missing', async () => {
  const { app, find } = await hub(games);
  assert.match(app.text, /Your games 3/);
  assert.match(app.text, /Neon Vow open here opened just now · 2 scenes/);
  assert.match(app.text, /Tetris opened|Tetris never opened · 1 scene /);
  assert.match(app.text, /Gone missing/);
  assert.equal(find((e) => e.tag === 'img')[0].attributes.src, 'data:image/jpeg;base64,neon');
  assert.equal(find((e) => e.tag === 'span' && e.className === 'letter')[0].text, 'T');
});

test('a picked game is the model\'s context, and its actions run the game tools', async () => {
  const { pick, button, sent, calls, app } = await hub(games, { project_play: { content: [{ type: 'text', text: 'The game is open' }] } });
  pick('/g/tetris').listeners.click();
  await settle();
  const update = sent.filter((m) => m.method === 'ui/update-model-context').at(-1).params;
  assert.deepEqual(plain(update.structuredContent), { game: 'Tetris', path: '/g/tetris', scenes: ['a'], opened: 0 });
  assert.deepEqual(plain(update.content[0].annotations), { audience: ['assistant'] });
  button('Play').listeners.click();
  await settle(); await settle();
  assert.deepEqual(calls(), [{ name: 'project_play', arguments: { game: '/g/tetris' } }, { name: 'games', arguments: {} }]);
  assert.match(app.text, /Tetris is open in its own window/);
  assert.equal(button('Open here') !== undefined, true);
});

test('a failed action says why', async () => {
  const { pick, button, app } = await hub(games, { project_export: { isError: true, content: [{ type: 'text', text: 'no engine' }] } });
  pick('/g/neon').listeners.click();
  assert.equal(button('Open here').attributes.disabled, '', 'already open here');
  button('Export').listeners.click();
  await settle(); await settle();
  assert.match(app.text, /Couldn't: no engine/);
});

test('a missing game offers nothing to do', async () => {
  const { pick, button } = await hub(games);
  pick('/g/gone').listeners.click();
  assert.equal(button('Play'), undefined);
});

test('a new game is made from its name', async () => {
  const { find, pick, button, calls } = await hub(games);
  button('New game').listeners.click();
  find((e) => e.tag === 'input')[0].listeners.input({ target: { value: '  Star Fox ' } });
  pick('/g/tetris').listeners.click();  // a render keeps the draft
  assert.equal(find((e) => e.tag === 'input')[0].value, '  Star Fox ');
  find((e) => e.tag === 'form')[0].listeners.submit({ preventDefault() {} });
  await settle();
  assert.deepEqual(calls()[0], { name: 'new_game', arguments: { name: 'Star Fox' } });
});

test('with no games yet, a game can still be made', async () => {
  const { app, button } = await hub({ content: [], structuredContent: { open: '', games: null } });
  assert.match(app.text, /No games yet/);
  assert.ok(button('New game'));
});

test('a pick made before the host took context reaches it once it does', async () => {
  const { pick, sent, fromInit } = await hub(games, {}, { delayInit: true });
  pick('/g/tetris').listeners.click();
  assert.equal(sent.filter((m) => m.method === 'ui/update-model-context').length, 0);
  fromInit();
  await settle(); await settle();
  assert.equal(plain(sent.filter((m) => m.method === 'ui/update-model-context').at(-1).params.structuredContent).path, '/g/tetris');
});

test('picks made while the host is still taking one end with the model on the last', async () => {
  const { pick, sent, held, find } = await hub(games, {}, { holdContext: true });
  const contexts = () => sent.filter((m) => m.method === 'ui/update-model-context').map((m) => m.params.structuredContent.path);
  pick('/g/neon').listeners.click();
  await settle();
  held.shift()();
  await settle();
  pick('/g/tetris').listeners.click();
  await settle();
  pick('/g/neon').listeners.click();
  held.shift()();  // Tetris taken after Neon Vow was picked again
  await settle(); await settle();
  assert.deepEqual(contexts(), ['/g/neon', '/g/tetris', '/g/neon']);
  assert.equal(find((e) => e.className === 'card on')[0].attributes.title, '/g/neon');
});

test('a game is picked with its own button, which keeps the focus for its actions', async () => {
  const { pick, button } = await hub(games);
  for (const path of ['/g/neon', '/g/tetris', '/g/gone']) assert.equal(pick(path).tag, 'button');
  pick('/g/tetris').listeners.click();
  assert.equal(focused.at, pick('/g/tetris'));
  assert.equal(pick('/g/tetris').attributes['aria-pressed'], 'true');
  assert.ok(button('Play'));
});

test('an answer that isn\'t games says so', async () => {
  const { app } = await hub({ isError: true, content: [{ type: 'text', text: 'boom' }] });
  assert.match(app.text, /boom/);
});
