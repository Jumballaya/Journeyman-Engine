const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
const bridge = fs.readFileSync(path.join(__dirname, 'bridge.js'), 'utf8');

// A widget's window in a host: an MCP Apps parent (sent: what it got), and
// window.openai when given.
function mount(openai) {
  const listeners = {};
  const sent = [];
  const parent = { postMessage(msg) { sent.push(msg); } };
  const window = { parent, openai, addEventListener(name, handler) { listeners[name] = handler; } };
  const context = vm.createContext({ window, setTimeout });
  vm.runInContext(bridge, context);
  const host = vm.runInContext('jmHost({ name: "t", version: "1.0.0" })', context);
  const fromHost = (data) => listeners.message({ source: parent, data: { jsonrpc: '2.0', ...data } });
  const answer = (method, result) => {
    const m = sent.filter((s) => s.method === method).at(-1);
    fromHost({ id: m.id, result });
    return m;
  };
  return { host, sent, fromHost, answer, listeners };
}
const settle = () => new Promise((resolve) => setImmediate(resolve));
const plain = (v) => JSON.parse(JSON.stringify(v));

test('a message asks ChatGPT not to send it, where the host takes openai/message', async () => {
  const { host, sent, answer } = mount();
  answer('ui/initialize', { hostCapabilities: { experimental: { 'openai/message': {} }, message: { text: {} } } });
  await settle();
  assert.ok(host.can('openai/message'));
  host.message([host.text('frame 60 of play p1', { title: 'Play p1 at 0:01.0' })], { send: false });
  await settle();
  assert.deepEqual(plain(sent.at(-1).params), {
    role: 'user',
    content: [{ type: 'text', text: 'frame 60 of play p1', _meta: { 'openai/title': 'Play p1 at 0:01.0' } }],
    _meta: { 'openai/message': { target: 'active', send: false } },
  });
});

test('a plain MCP Apps host gets a plain message', async () => {
  const { host, sent, answer } = mount();
  answer('ui/initialize', { hostCapabilities: {} });
  await settle();
  host.message('hello');
  await settle();
  assert.deepEqual(plain(sent.at(-1).params), { role: 'user', content: [{ type: 'text', text: 'hello' }] });
});

test('model context carries titled images and assistant-only text', async () => {
  const { host, sent, answer, fromHost } = mount();
  answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {}, image: {} } } });
  await settle();
  const done = host.updateContext({
    content: [host.image('data:image/jpeg;base64,AAAA', { title: 'Frame 60' }), host.text('{"frame":60}', { hidden: true })],
    structuredContent: { frame: 60 },
  });
  await settle();
  const m = answer('ui/update-model-context', {});
  assert.equal(await done, true);
  assert.deepEqual(plain(m.params), {
    content: [{ type: 'image', mimeType: 'image/jpeg', data: 'AAAA', _meta: { 'openai/title': 'Frame 60' } },
              { type: 'text', text: '{"frame":60}', annotations: { audience: ['assistant'] } }],
    structuredContent: { frame: 60 },
  });
});

test('a refused context update is false, not an error', async () => {
  const { host, sent, fromHost, answer } = mount();
  answer('ui/initialize', {});
  await settle();
  const done = host.updateContext({ content: [] });
  await settle();
  const m = sent.at(-1);
  fromHost({ id: m.id, error: { code: -32601, message: 'no' } });
  assert.equal(await done, false);
});

test('tool input, results and host context reach the widget', async () => {
  const { host, fromHost, answer } = mount();
  let changes = 0;
  host.onChange(() => changes++);
  answer('ui/initialize', { hostContext: { theme: 'dark', displayMode: 'fullscreen', 'openai/deepLink': { url: '/games/neon' } } });
  await settle();
  assert.equal(host.theme, 'dark');
  assert.equal(host.displayMode, 'fullscreen');
  assert.equal(host.deepLink, '/games/neon');
  fromHost({ method: 'ui/notifications/tool-input', params: { arguments: { file: { name: 'a.scene.json', resourceUri: 'h://1' } } } });
  assert.equal(host.input.file.name, 'a.scene.json');
  fromHost({ method: 'ui/notifications/tool-result', params: { content: [], structuredContent: { ok: 1 }, _meta: { k: 2 } } });
  assert.deepEqual(plain(host.result), { content: [], structuredContent: { ok: 1 }, _meta: { k: 2 } });
  assert.ok(changes >= 3);
});

test('in ChatGPT, window.openai serves until the MCP Apps host answers', async () => {
  const calls = [];
  const openai = {
    toolOutput: { id: 'p1' }, toolResponseMetadata: { 'jm/thumbs': [] }, toolInput: { play: 'latest' }, theme: 'light',
    callTool: async (name, args) => { calls.push(name); return { structuredContent: { via: 'openai' } }; },
    sendFollowUpMessage: async ({ prompt }) => { calls.push(prompt); },
  };
  const { host, sent, answer } = mount(openai);
  assert.deepEqual(plain(host.result), { structuredContent: { id: 'p1' }, _meta: { 'jm/thumbs': [] } });
  assert.deepEqual(plain(host.input), { play: 'latest' });
  assert.equal(host.can('updateContext'), false);
  assert.deepEqual(plain(await host.callTool('play_frame', {})), { structuredContent: { via: 'openai' }, _meta: {} });
  await host.message([host.text('a'), host.text('b', { title: 'B' })]);
  assert.deepEqual(calls, ['play_frame', 'a\n\nb']);

  answer('ui/initialize', { hostCapabilities: {} });
  await settle();
  host.callTool('play_frame', { at: '1' });
  assert.equal(sent.at(-1).method, 'tools/call');
  assert.equal(calls.length, 2);
  assert.equal(host.can('updateContext'), true);
});

test('a host with no frame and no window.openai offers nothing', () => {
  const context = vm.createContext({ window: { addEventListener() {} } });
  context.window.parent = context.window;
  vm.runInContext(bridge, context);
  const host = vm.runInContext('jmHost({ name: "t", version: "1" })', context);
  for (const feature of ['callTool', 'message', 'updateContext', 'displayMode']) assert.equal(host.can(feature), false);
});

test('a draft asked for before the host answered waits to learn if it can be one', async () => {
  const { host, sent, answer } = mount();
  host.message('draft', { send: false });
  await settle();
  assert.equal(sent.filter((m) => m.method === 'ui/message').length, 0);
  answer('ui/initialize', { hostCapabilities: { experimental: { 'openai/message': {} } } });
  await settle();
  assert.deepEqual(plain(sent.find((m) => m.method === 'ui/message').params._meta),
                   { 'openai/message': { target: 'active', send: false } });
});

test('once the MCP Apps host answered, window.openai\'s globals no longer replace its results', async () => {
  const openai = { toolOutput: { id: 'old' } };
  const { host, answer, fromHost, listeners } = mount(openai);
  answer('ui/initialize', {});
  await settle();
  fromHost({ method: 'ui/notifications/tool-result', params: { structuredContent: { id: 'new' } } });
  listeners['openai:set_globals']();
  assert.equal(host.result.structuredContent.id, 'new');
});

test('a draft on a host that takes none sends what it says instead', async () => {
  const { host, sent, answer } = mount();
  host.message([host.text('a moment', { title: 'T' })], { send: false, instead: 'a question' });
  answer('ui/initialize', { hostCapabilities: {} });
  await settle();
  assert.deepEqual(plain(sent.find((m) => m.method === 'ui/message').params), { role: 'user', content: [{ type: 'text', text: 'a question' }] });
});

test('window.openai globals that leave the tool output alone keep the result', () => {
  const openai = { toolOutput: { id: 'p1' } };
  const { host, listeners } = mount(openai);
  const first = host.result;
  openai.theme = 'dark';
  listeners['openai:set_globals']();
  assert.equal(host.result, first);
  assert.equal(host.theme, 'dark');
});
