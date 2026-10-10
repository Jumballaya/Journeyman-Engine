const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
const html = fs.readFileSync(path.join(__dirname, 'plays.html'), 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];

// Minimal DOM runs the shipped widget, including its real scrubbing handlers.
class Element {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.attributes = {};
    this.listeners = {};
    this.dataset = {};
    this.style = {};
    this.nodeType = 1;
  }
  append(...children) {
    for (const child of children) { if (child && child.nodeType) { child.parent = this; } }
    this.children.push(...children);
  }
  replaceWith(next) {
    this.parent.children[this.parent.children.indexOf(this)] = next;
    next.parent = this.parent;
  }
  replaceChildren() { this.children = []; }
  setAttribute(key, value) { this.attributes[key] = value; }
  addEventListener(name, handler) { this.listeners[name] = handler; }
  getBoundingClientRect() { return { left: 0, width: 800 }; }
  setPointerCapture() {}
  get text() {
    return this.children.map((child) => {
      if (typeof child === 'string') { return child; }
      return child.text;
    }).join(' ');
  }
}

const play = {
  id: 'test-run', game: 'VOIDLANCE', frames: 121, seconds: 2,
  sampleAt: [0, 60, 120], sampleTime: [0, 1, 2], scenes: [], values: [], markers: [], thumbs: [], stale: false,
};
const metadata = {
  'jm/thumbs': [
    { frame: 0, time: 0, src: 'data:image/png;base64,start' },
    { frame: 60, time: 1, src: 'data:image/png;base64,middle' },
    { frame: 120, time: 2, src: 'data:image/png;base64,end' },
  ],
};
function widget(output, meta = {}, selected = null, version = null, extra = {}) {
  const app = new Element('app');
  const listeners = {};
  const host = { toolOutput: output, toolResponseMetadata: meta, widgetState: { selected }, ...extra };
  const served = version ? script.replaceAll('__JM_VERSION__', version) : script;
  vm.runInNewContext(served, {
    window: { openai: host, addEventListener(name, handler) { listeners[name] = handler; } },
    document: {
      createElement(tag) { return new Element(tag); },
      createElementNS(namespace, tag) { return new Element(tag); },
      getElementById() { return app; },
      documentElement: new Element('html'), body: { scrollHeight: 600 },
      addEventListener(name, handler) { listeners['document:' + name] = handler; },
    },
    matchMedia() { return { matches: false, addEventListener() {} }; },
    requestAnimationFrame() {},
  });
  return { app, host, listeners };
}
function assertScrubs(app) {
  assert.match(app.text, /VOIDLANCE/);
  assert.match(app.text, /121 frames/);
  assert.doesNotMatch(app.text, /undefined|NaN/);
  const timeline = app.children.find((child) => { return child.className === 'timeline'; });
  timeline.listeners.pointerdown({ pointerId: 1, clientX: 400 });
  const shows60 = () => {
    assert.match(app.text, /0:01.0 · frame 60/);
    assert.doesNotMatch(app.text, /undefined|NaN/);
    const viewer = app.children.find((child) => { return child.className === 'viewer'; });
    const image = viewer.children.find((child) => { return child.tag === 'img'; });
    assert.equal(image.attributes.src, metadata['jm/thumbs'][1].src);
  };
  // Mid-drag the timeline stays (it holds the pointer); the moment updates around it.
  shows60();
  assert.equal(app.children.find((child) => { return child.className === 'timeline'; }), timeline);
  assert.equal(timeline.children.find((child) => { return child.className === 'cursor'; }).style.left, '50%');
  timeline.listeners.pointerup({ pointerId: 1 });
  shows60();
}

test('structured content and separate image metadata scrub correctly', () => {
  assertScrubs(widget(play, metadata).app);
});
test('full MCP result exposes its play and image metadata', () => {
  assertScrubs(widget({ structuredContent: play, content: [], _meta: metadata }).app);
});
test('full result also accepts metadata supplied separately by the host', () => {
  assertScrubs(widget({ structuredContent: play, content: [] }, metadata).app);
});
test('incomplete output waits for data and recovers on the host update', () => {
  const mounted = widget({});
  assert.doesNotMatch(mounted.app.text, /undefined|NaN/);
  assert.match(mounted.app.text, /Loading the play/);
  mounted.host.toolOutput = { structuredContent: play, _meta: metadata };
  mounted.listeners['openai:set_globals']();
  assertScrubs(mounted.app);
});
test('invalid persisted selections recover to a usable frame', () => {
  for (const selected of [NaN, -1, 121]) {
    assertScrubs(widget(play, metadata, selected).app);
  }
});
test('an answer that is not a play says so instead of drawing it', () => {
  for (const [output, says] of [[{ content: [{ type: 'text', text: 'no plays yet' }], isError: true }, /no plays yet/],
                                [{ id: 5 }, /isn't a play this timeline can show/]]) {
    const { app } = widget(output, {}, null, '1.0.0');
    assert.match(app.text, says);
    assert.match(app.text, /timeline from jm 1.0.0/);
    assert.doesNotMatch(app.text, /undefined|NaN/);
  }
});
test('a timeline older than its server says to reconnect', () => {
  const stale = widget({ ...play, jm: '1.1.0' }, metadata, null, '1.0.0').app;
  assert.match(stale.text, /timeline is from jm 1.0.0, the server is jm 1.1.0/);
  assertScrubs(stale);
  const current = widget({ ...play, jm: '1.0.0' }, metadata, null, '1.0.0').app;
  assert.doesNotMatch(current.text, /reconnect/);
});

// An MCP Apps host (Claude's kind): JSON-RPC over postMessage, no window.openai.
function mcpAppWidget() {
  const app = new Element('app');
  const listeners = {};
  const sent = [];
  const parent = { postMessage(msg) { sent.push(msg); } };
  const window = {
    parent,
    addEventListener(name, handler) { listeners[name] = handler; },
    dispatchEvent(event) { if (listeners[event.type]) listeners[event.type](event); },
  };
  vm.runInNewContext(script.replaceAll('__JM_VERSION__', '1.0.0'), {
    window, Event: class { constructor(type) { this.type = type; } },
    document: {
      createElement(tag) { return new Element(tag); },
      createElementNS(namespace, tag) { return new Element(tag); },
      getElementById() { return app; },
      documentElement: new Element('html'), body: { scrollHeight: 600 },
      addEventListener() {},
    },
    matchMedia() { return { matches: false, addEventListener() {} }; },
    requestAnimationFrame() {},
  });
  const fromHost = (data) => listeners.message({ source: parent, data: { jsonrpc: '2.0', ...data } });
  return { app, sent, fromHost };
}
const settle = () => new Promise((resolve) => setImmediate(resolve));

test('an MCP Apps host gets the timeline from its tool result, and its buttons call back', async () => {
  const { app, sent, fromHost } = mcpAppWidget();
  const init = sent.find((m) => m.method === 'ui/initialize');
  assert.ok(init, 'the widget introduces itself');
  assert.equal(init.params.appInfo.version, '1.0.0');
  fromHost({ id: init.id, result: { protocolVersion: '2026-01-26', hostContext: { theme: 'dark', displayMode: 'inline' } } });
  await settle();
  assert.ok(sent.some((m) => m.method === 'ui/notifications/initialized'));
  assert.match(app.text, /Loading the play/);

  fromHost({ method: 'ui/notifications/tool-result', params: { content: [], structuredContent: play, _meta: metadata } });
  assertScrubs(app);

  // Exact frame: a tools/call through the host, its image shown when it answers.
  const caption = app.children.find((child) => child.className === 'caption');
  const exact = caption.children.find((child) => child && child.text === 'Exact frame');
  exact.listeners.click();
  const call = sent.find((m) => m.method === 'tools/call');
  assert.equal(JSON.stringify(call.params), JSON.stringify({ name: 'play_frame', arguments: { play: 'test-run', at: '60' } }));
  fromHost({ id: call.id, result: { content: [], _meta: { 'jm/image': 'data:image/jpeg;base64,exact' },
                                    structuredContent: { play: 'test-run', frame: 60, time: 1, path: 'f.jpg', source: { kind: 'replay', drift: 'same' } } } });
  await settle();
  const viewer = app.children.find((child) => (child.className || '').startsWith('viewer'));
  assert.equal(viewer.children.find((child) => child.tag === 'img').attributes.src, 'data:image/jpeg;base64,exact');

  const ask = app.children.find((child) => child.className === 'caption').children.find((child) => child && child.text === 'Ask about this');
  ask.listeners.click();
  const message = sent.find((m) => m.method === 'ui/message');
  assert.equal(message.params.role, 'user');
  assert.match(message.params.content[0].text, /test-run/);
});

const child = (el, className) => el.children.find((c) => c && (c.className || '').split(' ')[0] === className);
const button = (app, label) => child(app, 'caption').children.find((c) => c && c.text === label);
const shown = (app) => {
  const viewer = child(app, 'viewer');
  const badge = child(viewer, 'exact');
  return { src: viewer.children.find((c) => c.tag === 'img').attributes.src, badge: badge ? badge.text : null };
};

// Exact frame on a ChatGPT host: callTool answers with play_frame's result.
async function exactFrameAnswers(result) {
  const calls = [];
  const callTool = async (name, args) => { calls.push(JSON.parse(JSON.stringify({ name, args }))); return result; };
  const { app } = widget(play, metadata, 60, null, { callTool });
  button(app, 'Exact frame').listeners.click();
  await settle();
  assert.deepEqual(calls, [{ name: 'play_frame', args: { play: 'test-run', at: '60' } }]);
  return app;
}
const replayed = (source) => ({
  content: [{ type: 'text', text: 'prose the widget must not read' }],
  structuredContent: { play: 'test-run', frame: 60, time: 1, path: 'f.png', source },
  _meta: { 'jm/image': 'data:image/png;base64,replayed' },
});

test('a replay is labeled by how far the game drifted', async () => {
  for (const [drift, badge] of [['same', 'exact frame'], ['look', 'exact frame · current art'],
                                ['game', 'replayed with the current build']]) {
    const app = await exactFrameAnswers(replayed({ kind: 'replay', drift }));
    assert.deepEqual(shown(app), { src: 'data:image/png;base64,replayed', badge });
    assert.equal(button(app, 'Exact frame').attributes.disabled, '');
  }
});
test('a thumbnail fallback is not called exact, and the button stays usable', async () => {
  const app = await exactFrameAnswers(replayed({ kind: 'thumbnail', frame: 60 }));
  assert.equal(shown(app).badge, null);
  assert.equal(shown(app).src, metadata['jm/thumbs'][1].src);
  assert.equal(button(app, 'Exact frame').attributes.disabled, undefined);
  assert.match(app.text, /Nothing here can replay it: the nearest thumbnail is frame 60/);
});
test('only the selection is restored from saved widget state', () => {
  const saved = { selected: 60, frames: { 60: { src: 'data:stale', label: 'exact frame' } }, pending: 'exact', notice: 'old news',
                  exact: { 60: 'data:stale' }, status: 'old news', working: true };
  const { app } = widget(play, metadata, null, null, { widgetState: saved, callTool() {} });
  assert.match(app.text, /0:01.0 · frame 60/);
  assert.deepEqual(shown(app), { src: metadata['jm/thumbs'][1].src, badge: null });
  assert.equal(button(app, 'Exact frame').attributes.disabled, undefined);
  assert.doesNotMatch(app.text, /old news/);
});

test('a frame\'s time comes from the sampled points, however uneven', () => {
  // 100 frames in the first second, 20 in the next: frame 110 is at 1.5s, not 1.83s.
  const uneven = { ...play, sampleAt: [0, 100, 120], sampleTime: [0, 1, 2] };
  assert.match(widget(uneven, {}, 110).app.text, /0:01.5 · frame 110/);
  assert.match(widget(uneven, {}, 50).app.text, /0:00.5 · frame 50/);
  // Without samples, frames are taken as even.
  assert.match(widget({ ...play, sampleAt: undefined, sampleTime: undefined }, {}, 30).app.text, /0:00.5 · frame 30/);
});
test('arrow keys step to the neighbouring thumbnail, and stop at the ends', () => {
  const { app, listeners } = widget(play, metadata, 70);
  const key = (k) => listeners['document:keydown']({ key: k });
  key('ArrowRight');
  assert.match(app.text, /frame 120/);
  key('ArrowRight');
  assert.match(app.text, /frame 120/);
  key('ArrowLeft'); key('ArrowLeft'); key('ArrowLeft');
  assert.match(app.text, /0:00.0 · frame 0/);
});
