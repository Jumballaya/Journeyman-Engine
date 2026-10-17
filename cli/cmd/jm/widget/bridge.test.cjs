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
  await settle();
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

test('context the person removed stays out until it changes; background context still goes', async () => {
  const { host, sent, answer, fromHost } = mount();
  answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } },
    hostContext: { 'openai/modelContext': { updateId: 'u1', content: [{ type: 'text', text: 'Neon Vow', _meta: { 'openai/title': 'Neon Vow' } }] } } });
  await settle();
  assert.equal(host.attached.content[0].text, 'Neon Vow');
  const game = host.text('Neon Vow', { title: 'Neon Vow' });
  fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': null } });
  assert.equal(host.attached, null);
  const hidden = host.text('on the Overview', { hidden: true });
  host.updateContext({ content: [game, hidden] });
  await settle();
  assert.deepEqual(plain(sent.at(-1).params.content), [plain(hidden)]);
  answer('ui/update-model-context', {});
  await settle();
  const other = host.text('Tetris', { title: 'Tetris', thumbnail: 'data:image/jpeg;base64,AAAA' });
  host.updateContext({ content: [other, hidden] });
  await settle();
  assert.deepEqual(plain(sent.at(-1).params.content[0]), { type: 'text', text: 'Tetris',
    _meta: { 'openai/title': 'Tetris', 'openai/thumbnail': { src: 'data:image/jpeg;base64,AAAA' } } });
});

test('a resource link names its file', () => {
  const { host } = mount();
  assert.deepEqual(plain(host.link('file:///g/scenes/a.scene.json', { mimeType: 'application/json' })),
    { type: 'resource_link', uri: 'file:///g/scenes/a.scene.json', name: 'a.scene.json', mimeType: 'application/json' });
});

test('an item the person removed stays out when the host keeps the rest', async () => {
  const { host, sent, answer, fromHost } = mount();
  answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
  await settle();
  const game = host.text('Neon Vow', { title: 'Neon Vow' }), hidden = host.text('Overview', { hidden: true });
  host.updateContext({ content: [game, hidden] });
  await settle();
  answer('ui/update-model-context', {});
  await settle();
  fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': { updateId: 'u2', content: [plain(hidden)] } } });
  host.updateContext({ content: [game, hidden] });
  await settle();
  assert.deepEqual(plain(sent.at(-1).params.content), [plain(hidden)]);
});

test('ChatGPT desktop older than the extensions is told to update; other hosts never', async () => {
  const at = async (version, caps, platform = 'desktop', name = 'host') => {
    const { host, answer } = mount();
    answer('ui/initialize', { hostInfo: { name, version }, hostCapabilities: caps, hostContext: { platform } });
    await settle();
    return host.outdated();
  };
  const chatgpt = { experimental: { 'openai/message': {} } };
  assert.equal(await at('26.927.99999', chatgpt), true);
  assert.equal(await at('26.928.9999', chatgpt), true);
  assert.equal(await at('26.928.20710', chatgpt), false);
  assert.equal(await at('27.1.0', chatgpt), false);
  assert.equal(await at('1.0.0', {}), false);  // Claude desktop: not ChatGPT
  assert.equal(await at('26.900.0', chatgpt, 'web'), false);
  assert.equal(await at('26.900.0', {}, 'desktop', 'ChatGPT'), true);  // too old to say its extensions
});

test('context the page replaced isn\'t taken as removed, whichever the host says first', async () => {
  for (const notifyFirst of [true, false]) {
    const { host, sent, answer, fromHost } = mount();
    answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
    await settle();
    const a = host.text('Neon Vow', { title: 'Neon Vow' }), b = host.text('Tetris', { title: 'Tetris' });
    const held = (block, id) => fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': { updateId: id, content: [plain(block)] } } });
    host.updateContext({ content: [a] });
    await settle();
    answer('ui/update-model-context', {});
    await settle();
    held(a, 'u1');
    host.updateContext({ content: [b] });
    await settle();
    if (notifyFirst) held(b, 'u2');
    answer('ui/update-model-context', {});
    await settle();
    if (!notifyFirst) held(b, 'u2');
    host.updateContext({ content: [a] });
    await settle();
    assert.deepEqual(plain(sent.at(-1).params.content), [plain(a)], notifyFirst ? 'notified before the reply' : 'after');
  }
});

test('after a refused update, what the person removes still stays out', async () => {
  const { host, sent, answer, fromHost } = mount();
  answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
  await settle();
  const a = host.text('Neon Vow', { title: 'Neon Vow' }), b = host.text('Tetris', { title: 'Tetris' });
  host.updateContext({ content: [a] });
  await settle();
  answer('ui/update-model-context', {});
  await settle();
  host.updateContext({ content: [b] });
  await settle();
  fromHost({ id: sent.at(-1).id, error: { code: -1, message: 'no' } });
  await settle();
  fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': null } });
  host.updateContext({ content: [a] });
  await settle();
  assert.deepEqual(plain(sent.at(-1).params.content), []);
});

test('a refused update doesn\'t undo a later one the host took', async () => {
  const { host, sent, answer, fromHost } = mount();
  answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
  await settle();
  const [a, b, c] = ['A', 'B', 'C'].map((t) => host.text(t, { title: t }));
  host.updateContext({ content: [a] });
  await settle();
  answer('ui/update-model-context', {});
  await settle();
  host.updateContext({ content: [b] });
  host.updateContext({ content: [c] });
  await settle();
  const [toB, toC] = sent.filter((m) => m.method === 'ui/update-model-context').slice(-2);
  fromHost({ id: toB.id, error: { code: -1, message: 'no' } });
  fromHost({ id: toC.id, result: {} });
  await settle();
  fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': null } });
  host.updateContext({ content: [c] });
  await settle();
  assert.deepEqual(plain(sent.at(-1).params.content), []);
});

test('updates all refused leave removals working', async () => {
  const { host, sent, answer, fromHost } = mount();
  answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
  await settle();
  const [a, b, c] = ['A', 'B', 'C'].map((t) => host.text(t, { title: t }));
  host.updateContext({ content: [a] });
  await settle();
  answer('ui/update-model-context', {});
  await settle();
  host.updateContext({ content: [b] });
  host.updateContext({ content: [c] });
  await settle();
  for (const m of sent.filter((m) => m.method === 'ui/update-model-context').slice(-2)) fromHost({ id: m.id, error: { code: -1, message: 'no' } });
  await settle();
  fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': null } });
  host.updateContext({ content: [a] });
  await settle();
  assert.deepEqual(plain(sent.at(-1).params.content), []);
});

test('a tool call waits for the host to answer ui/initialize', async () => {
  const { host, sent, answer } = mount();
  const got = host.callTool('plays_list', {});
  await settle();
  assert.equal(sent.filter((m) => m.method === 'tools/call').length, 0);
  answer('ui/initialize', {});
  await settle();
  answer('tools/call', { structuredContent: { plays: [] } });
  assert.deepEqual(plain(await got), { structuredContent: { plays: [] } });
});

test('what the person removes while a replacement is on its way stays out, taken or refused', async () => {
  for (const [notice, outcome] of [[null, 'taken'], [null, 'refused'], ['background', 'taken'], ['background', 'refused']]) {
    const { host, sent, answer, fromHost } = mount();
    answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
    await settle();
    const a = host.text('Neon Vow', { title: 'Neon Vow' }), b = host.text('Tetris', { title: 'Tetris' });
    const bg = host.text('on the Overview', { hidden: true });
    host.updateContext({ content: [a, bg] });
    await settle();
    answer('ui/update-model-context', { updateId: 'u1' });
    await settle();
    host.updateContext({ content: [b, bg] });
    await settle();
    const toB = sent.at(-1);
    fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': notice && { updateId: 'p1', content: [plain(bg)] } } });
    if (outcome === 'taken') fromHost({ id: toB.id, result: { updateId: 'u2' } });
    else fromHost({ id: toB.id, error: { code: -1, message: 'no' } });
    await settle();
    host.updateContext({ content: [a, bg] });
    await settle();
    assert.deepEqual(plain(sent.at(-1).params.content), [plain(bg)], `${notice || 'null'}, ${outcome}`);
  }
});

test('a removal while a background-only update is on its way stays out, taken or refused', async () => {
  for (const [notice, outcome] of [[null, 'taken'], [null, 'refused'], ['old background', 'taken'], ['old background', 'refused']]) {
    const { host, sent, answer, fromHost } = mount();
    answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
    await settle();
    const a = host.text('Neon Vow', { title: 'Neon Vow' });
    const oldBg = host.text('on the Overview', { hidden: true }), newBg = host.text('on Scenes', { hidden: true });
    host.updateContext({ content: [a, oldBg] });
    await settle();
    answer('ui/update-model-context', { _meta: { 'openai/modelContext': { updateId: 'u1' } } });
    await settle();
    host.updateContext({ content: [newBg], structuredContent: { tab: 'scenes' } });
    await settle();
    const toBg = sent.at(-1);
    fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': notice && { updateId: 'person-1', content: [plain(oldBg)] } } });
    if (outcome === 'taken') fromHost({ id: toBg.id, result: { _meta: { 'openai/modelContext': { updateId: 'u2' } } } });
    else fromHost({ id: toBg.id, error: { code: -1, message: 'no' } });
    await settle();
    host.updateContext({ content: [a, oldBg], structuredContent: { tab: 'overview' } });
    await settle();
    const last = plain(sent.at(-1).params);
    assert.deepEqual(last.content, [plain(oldBg)], `${notice || 'null'}, ${outcome}`);
    assert.deepEqual(last.structuredContent, { tab: 'overview' });
  }
});

test('the page\'s own update, named by the id in its reply, isn\'t a removal, whichever comes first', async () => {
  for (const noticeFirst of [true, false]) {
    const { host, sent, answer, fromHost } = mount();
    answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
    await settle();
    const a = host.text('Neon Vow', { title: 'Neon Vow' }), bg = host.text('on Scenes', { hidden: true });
    host.updateContext({ content: [a] });
    await settle();
    answer('ui/update-model-context', {});
    await settle();
    host.updateContext({ content: [bg] });
    await settle();
    const toBg = sent.at(-1);
    const notice = () => fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': { updateId: 'u2', content: [{ type: 'text', text: 'on Scenes' }] } } });
    if (noticeFirst) notice();
    fromHost({ id: toBg.id, result: { _meta: { 'openai/modelContext': { updateId: 'u2' } } } });
    await settle();
    if (!noticeFirst) notice();
    host.updateContext({ content: [a] });
    await settle();
    assert.deepEqual(plain(sent.at(-1).params.content), [plain(a)], noticeFirst ? 'notice first' : 'reply first');
  }
});

test('the page\'s own update echoed with its keys in another order isn\'t a removal', async () => {
  const { host, sent, answer, fromHost } = mount();
  answer('ui/initialize', { hostCapabilities: { updateModelContext: { text: {} } } });
  await settle();
  const a = host.text('Neon Vow', { title: 'Neon Vow' }), bg = host.text('on Scenes', { hidden: true });
  host.updateContext({ content: [a] });
  await settle();
  answer('ui/update-model-context', {});
  await settle();
  host.updateContext({ content: [bg], structuredContent: { tab: 'scenes', game: '/g/neon' } });
  await settle();
  const toBg = sent.at(-1);
  fromHost({ method: 'ui/notifications/host-context-changed', params: { 'openai/modelContext': { structuredContent: { game: '/g/neon', tab: 'scenes' }, content: [plain(bg)] } } });
  fromHost({ id: toBg.id, result: {} });
  await settle();
  host.updateContext({ content: [a] });
  await settle();
  assert.deepEqual(plain(sent.at(-1).params.content), [plain(a)]);
});
