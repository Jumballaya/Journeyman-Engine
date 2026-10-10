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
    this.nodeType = 1;
  }
  append(...children) { this.children.push(...children); }
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
  play: 'test-run', game: 'VOIDLANCE', frames: 121, seconds: 2,
  sampleTime: [0, 1, 2], scenes: [], values: [], markers: [],
};
const metadata = {
  'jm/thumbs': [
    { frame: 0, time: 0, src: 'data:image/png;base64,start' },
    { frame: 60, time: 1, src: 'data:image/png;base64,middle' },
    { frame: 120, time: 2, src: 'data:image/png;base64,end' },
  ],
};
function widget(output, meta = {}, selected = null, version = null) {
  const app = new Element('app');
  const listeners = {};
  const host = { toolOutput: output, toolResponseMetadata: meta, widgetState: { selected } };
  const served = version ? script.replaceAll('__JM_VERSION__', version) : script;
  vm.runInNewContext(served, {
    window: { openai: host, addEventListener(name, handler) { listeners[name] = handler; } },
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
  return { app, host, listeners };
}
function assertScrubs(app) {
  assert.match(app.text, /VOIDLANCE/);
  assert.match(app.text, /121 frames/);
  assert.doesNotMatch(app.text, /undefined|NaN/);
  const timeline = app.children.find((child) => { return child.className === 'timeline'; });
  timeline.listeners.pointerdown({ pointerId: 1, clientX: 400 });
  assert.match(app.text, /0:01.0 · frame 60/);
  assert.doesNotMatch(app.text, /undefined|NaN/);
  const viewer = app.children.find((child) => { return child.className === 'viewer'; });
  const image = viewer.children.find((child) => { return child.tag === 'img'; });
  assert.equal(image.attributes.src, metadata['jm/thumbs'][1].src);
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
                                [{ play: 5 }, /isn't a play this timeline can show/]]) {
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
