const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
// The page as jm serves it: the bridge and kit inlined, then the scene page's own script.
const html = fs.readFileSync(path.join(__dirname, 'scene.html'), 'utf8')
  .replace('/*__JM_BRIDGE__*/', ['bridge.js', 'kit.js'].map((f) => fs.readFileSync(path.join(__dirname, f), 'utf8')).join('\n'));
const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]).join('\n');

class Element {
  constructor(tag) { Object.assign(this, { tag, children: [], attributes: {}, listeners: {}, dataset: {}, nodeType: 1 }); }
  append(...kids) { this.children.push(...kids); }
  replaceChildren() { this.children = []; }
  setAttribute(k, v) { this.attributes[k] = v; }
  addEventListener(name, fn) { this.listeners[name] = fn; }
  get text() { return this.children.map((c) => (typeof c === 'string' ? c : c.text)).join(' '); }
  all(pred) { return [...(pred(this) ? [this] : []), ...this.children.flatMap((c) => (c.nodeType ? c.all(pred) : []))]; }
}
const settle = async () => { for (let i = 0; i < 5; i++) await new Promise((resolve) => setImmediate(resolve)); };
const plain = (v) => JSON.parse(JSON.stringify(v));

const file = { name: 'level.scene.json', resourceUri: 'host-resource://level' };
const raw = '{\n  "name": "Level",\n  "entities": [\n    {\n      "name": "Kage",\n      "components": {\n        "TransformComponent": {\n          "position": [10, 20, 0]\n        }\n      }\n    }\n  ]\n}\n';
const laid = (x) => ({ content: [], structuredContent: { scene: 'scenes/level.scene.json', game: 'Pip', root: '/g/pip', name: 'Level', entities: [
  { where: 'entities[0]', name: 'Kage', x, y: 20, w: 0, h: 0, components: { TransformComponent: { position: [x, 20, 0] } } }] } });

// The page in ChatGPT desktop's file entrypoint: answers are what the host says
// to each method (a function of the request, or a result).
async function scenePage({ caps = { experimental: { 'openai/resource': {} }, updateModelContext: { text: {} } }, input = { file }, answers = {} } = {}) {
  const app = new Element('app');
  const listeners = {};
  const sent = [];
  const parent = {
    postMessage(m) {
      sent.push(m);
      if (m.id === undefined) return;
      const answer = m.method === 'ui/initialize' ? { hostCapabilities: caps }
        : m.method === 'tools/call' ? answers[m.params.name] : answers[m.method];
      const result = typeof answer === 'function' ? answer(m) : answer || {};
      setImmediate(() => listeners.message({ source: parent, data: { jsonrpc: '2.0', id: m.id, result } }));
    },
  };
  vm.runInNewContext(script, {
    window: { parent, addEventListener(name, fn) { listeners[name] = fn; } }, setTimeout, TextDecoder, atob,
    document: { createElement: (t) => new Element(t), createElementNS: (ns, t) => new Element(t), getElementById: () => app,
                documentElement: new Element('html'), body: { scrollHeight: 500 } },
    matchMedia: () => ({ matches: false, addEventListener() {} }),
  });
  const fromHost = (method, params) => listeners.message({ source: parent, data: { jsonrpc: '2.0', method, params } });
  await settle();
  if (input) fromHost('ui/notifications/tool-input', { arguments: input });
  fromHost('ui/notifications/tool-result', laid(10));
  await settle();
  const find = (pred) => app.all(pred);
  return { app, sent, find, fromHost, of: (method) => sent.filter((m) => m.method === method) };
}
const pick = (find, name) => find((e) => e.tag === 'button' && e.text.trim() === name)[0].listeners.click();
const xInput = (find) => find((e) => e.tag === 'input' && e.attributes.type === 'number')[0];

test('an edit is written through the host with its version, in jm\'s layout, and the scene laid out again', async () => {
  const { find, of, app } = await scenePage({ answers: {
    'resources/read': { contents: [{ uri: file.resourceUri, text: raw, _meta: { 'openai/resource': { etag: 'v1', writable: true } } }] },
    'openai/resources/write': { outcome: 'saved', etag: 'v2' },
    scene_view: laid(42),
  } });
  assert.equal(plain(of('resources/read')[0].params).uri, file.resourceUri);
  pick(find, 'Kage');
  xInput(find).listeners.change({ target: { value: '42' } });
  await settle();
  const write = plain(of('openai/resources/write')[0].params);
  assert.equal(write.ifMatch, 'v1');
  assert.equal(write.text, raw.replace('[10, 20, 0]', '[42, 20, 0]'));
  assert.deepEqual(plain(of('tools/call').at(-1).params), { name: 'scene_view', arguments: { file } });
  assert.match(app.text, /Saved/);
  assert.equal(xInput(find).attributes.value, '42');
  assert.equal(plain(of('ui/update-model-context').at(-1).params).structuredContent.x, 42, 'the model sees the saved value');
});

const reading = (text, meta) => ({ 'resources/read': { contents: [{ text, _meta: { 'openai/resource': meta } }] } });
test('a file the page can\'t write back as it was, or without a version to check, is read only', async () => {
  for (const [answers, why] of [[reading(raw.replace('[10, 20, 0]', '[10,20,0]'), { etag: 'v1', writable: true }), /jm fmt it/],
                                [reading(raw.replace('10', '9007199254740993'), { etag: 'v1', writable: true }), /jm fmt it/],
                                [reading(raw, { writable: true }), /won't save it/]]) {
    const { find, app } = await scenePage({ answers });
    pick(find, 'Kage');
    assert.equal(xInput(find), undefined);
    assert.match(app.text, why);
  }
});

test('a prefab\'s child is shown, not edited', async () => {
  const shadow = { where: 'entities[0]/Shadow', name: 'Shadow', inherited: true, x: 1, y: 2, w: 0, h: 0, components: { TransformComponent: { position: [0, 4, 0] } } };
  const result = laid(10); result.structuredContent.entities.push(shadow);
  const { find, fromHost } = await scenePage({ answers: reading(raw, { etag: 'v1', writable: true }) });
  fromHost('ui/notifications/tool-result', result);
  await settle();
  pick(find, 'Shadow');
  assert.equal(xInput(find), undefined);
});

test('a file changed elsewhere isn\'t overwritten: it\'s read again', async () => {
  let reads = 0;
  const { find, of, app } = await scenePage({ answers: {
    'resources/read': () => ({ contents: [{ text: raw, _meta: { 'openai/resource': { etag: `v${++reads}`, writable: true } } }] }),
    'openai/resources/write': { outcome: 'conflict', etag: 'v9' },
  } });
  pick(find, 'Kage');
  xInput(find).listeners.change({ target: { value: '42' } });
  await settle();
  assert.equal(of('resources/read').length, 2);
  assert.match(app.text, /changed since/);
  assert.equal(plain(of('tools/call').at(-1).params).name, 'scene_view', 'laid out again');
  assert.match(app.text, /Pick an entity/, 'the picked place may be another entity now');
  const context = () => plain(of('ui/update-model-context').at(-1).params).structuredContent;
  assert.equal(context().where, undefined, 'the model no longer has the entity');
  assert.equal(context().scene, 'scenes/level.scene.json');
  pick(find, 'Kage');
  await settle();
  assert.equal(context().where, 'entities[0]');
});

test('opened by the agent (no file), or by a host that can\'t write it, the scene is read only', async () => {
  for (const options of [{ input: null }, { caps: {} }]) {
    const { find, of, app } = await scenePage(options);
    pick(find, 'Kage');
    assert.equal(xInput(find), undefined);
    assert.equal(of('openai/resources/write').length, 0);
    assert.match(app.text, /Read only here/);
  }
});

test('the entity picked is the model\'s context, for it alone', async () => {
  const { find, of } = await scenePage();
  pick(find, 'Kage');
  await settle();
  const update = plain(of('ui/update-model-context').at(-1).params);
  assert.equal(update.structuredContent.where, 'entities[0]');
  assert.equal(update.content[0]._meta['openai/title'], 'Kage');  // picked: an item in the person's chat
  assert.deepEqual(update.content[1].annotations, { audience: ['assistant'] });
});

test('ask about the picked entity: titled, in the draft or a new chat', async () => {
  const { find, of } = await scenePage({ caps: { experimental: { 'openai/message': {} } } });
  pick(find, 'Kage');
  await settle();
  find((e) => e.tag === 'button' && e.text === 'New chat')[0].listeners.click();
  await settle();
  const m = plain(of('ui/message').at(-1).params);
  assert.equal(m.content[0]._meta['openai/title'], 'Kage');
  assert.match(m.content[0].text, /^Kage \(entities\[0\]\) in /);
  assert.deepEqual(m._meta['openai/message'], { target: 'new', send: false });
});

test('Rendered shows the scene as the engine draws it beside its layout, or why it couldn\'t, and hides it again', async () => {
  const drawn = { content: [], _meta: { 'jm/image': 'data:image/jpeg;base64,x' } };
  const { find, of } = await scenePage({ answers: { scene_render: drawn } });
  pick(find, 'Rendered');
  await settle();
  // The file the host opened is drawn in its own game.
  assert.deepEqual(plain(of('tools/call').find((m) => m.params.name === 'scene_render').params.arguments), { file });
  assert.equal(find((e) => e.tag === 'img')[0].attributes.src, 'data:image/jpeg;base64,x');
  pick(find, 'Rendered');
  assert.equal(find((e) => e.tag === 'img').length, 0);
  const agents = await scenePage({ input: null, answers: { scene_render: drawn } });
  pick(agents.find, 'Rendered');
  await settle();
  assert.deepEqual(plain(agents.of('tools/call').find((m) => m.params.name === 'scene_render').params.arguments), { scene: 'scenes/level.scene.json', game: '/g/pip' });
  // Another scene shown before it's drawn: the drawing isn't put under it.
  let finish;
  const slow = await scenePage({ answers: { scene_render: () => new Promise((f) => { finish = f; }) } });
  pick(slow.find, 'Rendered');
  await settle();
  slow.fromHost('ui/notifications/tool-result', laid(30));
  await settle();
  finish(drawn);
  await settle();
  assert.equal(slow.find((e) => e.tag === 'img').length, 0);
  const failed = await scenePage({ answers: { scene_render: { isError: true, content: [{ type: 'text', text: 'the build failed' }] } } });
  pick(failed.find, 'Rendered');
  await settle();
  assert.match(failed.app.text, /the build failed/);
});

test('the screen as the game starts frames the layout: a lone small entity stays small', async () => {
  const { find, fromHost } = await scenePage();
  const r = laid(10);
  r.structuredContent.screen = { x: 0, y: 0, w: 1280, h: 720, camera: 1 };
  fromHost('ui/notifications/tool-result', r);
  await settle();
  const screen = find((e) => e.attributes.class === 'screen')[0];
  assert.deepEqual([screen.attributes.x, screen.attributes.y, screen.attributes.width, screen.attributes.height], [-640, -360, 1280, 720]);
  const svg = find((e) => e.tag === 'svg')[0];
  assert.match(svg.attributes.viewBox, /^-691\.2 -411\.2 1382\.4 822\.4$/);
  assert.equal(svg.attributes.style, 'width: 100%');  // a screen fits the view
});

test('a level 50 screens long opens on its screen, and its points stay big enough to pick', async () => {
  const { find, fromHost } = await scenePage();
  const r = laid(64000);
  r.structuredContent.screen = { x: 0, y: 0, w: 1280, h: 720, camera: 1 };
  fromHost('ui/notifications/tool-result', r);
  await settle();
  const svg = find((e) => e.tag === 'svg')[0];
  assert.match(svg.attributes.viewBox, /^-691\.2 -411\.2 /);  // the margin is the screen's, not 4% of the level
  assert.equal(svg.attributes.style, 'width: 4683%');  // (640 + 64000 + 2 × 51.2) / 1382.4
  assert.equal(find((e) => e.tag === 'circle')[0].attributes.r, 4 * 1382.4 / 640);  // 4 px in a 640 px view
});

test('without a camera entity (a script may move it), the whole layout fits the view; a long level still scrolls', async () => {
  const { find, fromHost } = await scenePage();
  const r = laid(1500);
  r.structuredContent.screen = { x: 0, y: 0, w: 1280, h: 720 };
  fromHost('ui/notifications/tool-result', r);
  await settle();
  assert.equal(find((e) => e.tag === 'svg')[0].attributes.style, 'width: 100%');
  assert.match(find((e) => e.attributes.class === 'screen')[0].children[0].text, /before a script moves the camera/);
  const long = laid(64000);
  long.structuredContent.screen = { x: 0, y: 0, w: 1280, h: 720 };
  fromHost('ui/notifications/tool-result', long);
  await settle();
  assert.equal(find((e) => e.tag === 'svg')[0].attributes.style, 'width: 4683%');  // screens across, as with a camera
});

test('each field is named for assistive tech: its label points at it, a vector\'s values by axis', async () => {
  const { find } = await scenePage({ answers: {
    'resources/read': { contents: [{ uri: file.resourceUri, text: raw, _meta: { 'openai/resource': { etag: 'v1', writable: true } } }] } } });
  pick(find, 'Kage');
  await settle();
  const label = find((e) => e.tag === 'label' && e.text === 'name')[0];
  const input = find((e) => e.tag === 'input' && e.attributes.type === 'text')[0];
  assert.ok(label.attributes.for);
  assert.equal(label.attributes.for, input.attributes.id);
  const position = find((e) => e.tag === 'input' && e.attributes.type === 'number').map((e) => e.attributes['aria-label']);
  assert.deepEqual(position, ['position x', 'position y', 'position z']);
  assert.equal(find((e) => e.tag === 'label' && e.text === 'position')[0].attributes.for, find((e) => e.tag === 'input' && e.attributes.type === 'number')[0].attributes.id);
});

test('the scene is drawn y up, each shape turned as the engine turns it', async () => {
  const { find, fromHost } = await scenePage();
  const bar = { where: 'entities[0]', name: 'Bar', x: 100, y: 50, w: 16, h: 8, rotation: Math.PI / 2, components: {} };
  fromHost('ui/notifications/tool-result', { content: [], structuredContent: { scene: 'scenes/level.scene.json', entities: [bar] } });
  await settle();
  const rect = find((e) => e.tag === 'rect')[0];
  assert.deepEqual([rect.attributes.x, rect.attributes.y], [92, -54]);
  assert.equal(rect.attributes.transform, 'rotate(-90 100 -50)');
  const [x, y, w, h] = find((e) => e.tag === 'svg')[0].attributes.viewBox.split(' ').map(Number);
  const m = 16 * 0.04;  // the margin
  assert.deepEqual([x + m, y + m, w - 2 * m, h - 2 * m].map((v) => Math.round(v * 1e6) / 1e6), [96, -58, 8, 16], 'turned, it stands 8 wide and 16 tall');
});

test('the pages write JSON as jm fmt does', () => {
  const kit = vm.runInNewContext(`${fs.readFileSync(path.join(__dirname, 'kit.js'), 'utf8')}; kit`, {});
  const want = fs.readFileSync(path.join(__dirname, '../../../internal/jsonfmt/testdata/scene.out.json'), 'utf8');
  assert.equal(kit.jsonText(JSON.parse(want)) + '\n', want);
});

test('each sprite is drawn as it looks, lowest z first, its box over it to pick', async () => {
  const { find, fromHost } = await scenePage({ input: null });
  const sprite = (name, z, texture, extra = {}) => ({ where: `entities[${z}]`, name, x: 0, y: 0, z, w: 16, h: 16, look: texture || undefined, ...extra,
    components: { TransformComponent: { scale: [8, 8] }, SpriteComponent: texture === null ? { color: [1, 0, 0, 0.5] } : { texture, color: [0.5, 1, 1, 1] } } });
  const bare = { where: 'entities[3]', name: 'Bare', x: 50, y: 0, w: 0, h: 0, components: { TransformComponent: {}, SpriteComponent: {} } };
  const loose = { where: 'entities[4]', name: 'Loose', x: 0, y: 0, w: 0, h: 0, components: { SpriteComponent: {} } };
  fromHost('ui/notifications/tool-result', { content: [], _meta: { 'jm/sprites': { 'a.png': 'data:image/png;base64,AAA' } },
    structuredContent: { scene: 'scenes/level.scene.json', entities: [sprite('Top', 2, 'a.png', { flipX: true }), sprite('Quad', 1, null), sprite('Missing', 0, 'gone.png'), bare, loose] } });
  await settle();
  const drawn = find((e) => e.attributes.class === 'sprite');
  // The unscaled quad (z 0) is the engine's 2 × 2, under the quad (z 1), under the picture (z 2); without a transform, none.
  assert.deepEqual(drawn.map((e) => e.tag), ['rect', 'rect', 'image']);
  assert.deepEqual([drawn[0].attributes.width, drawn[0].attributes.x], [2, 49]);
  drawn.shift();
  assert.equal(drawn[0].attributes.fill, 'rgb(255 0 0)');
  assert.equal(drawn[0].attributes.opacity, 0.5);
  assert.equal(drawn[1].attributes.href, 'data:image/png;base64,AAA');
  assert.match(drawn[1].attributes.transform, /scale\(-1 1\)/);
  const filter = find((e) => e.tag === 'feColorMatrix')[0];
  assert.equal(drawn[1].attributes.filter, 'url(#tint0)');  // its color tints its picture
  assert.match(filter.attributes.values, /^0\.5 0 0 0 0 0 1 /);
  const boxes = find((e) => (e.attributes.class || '').startsWith('shape'));
  assert.deepEqual(boxes.map((e) => e.attributes.class.includes('drawn')), [true, true, false, true, false]);  // no picture: its box as before
});

test('a tile map is drawn whole, standing on its transform, under the sprites; without a transform, not at all', async () => {
  const { find, fromHost } = await scenePage({ input: null });
  const map = (name, components) => ({ where: name, name, x: 10, y: 20, w: 0, h: 0, components });
  fromHost('ui/notifications/tool-result', { content: [], _meta: { 'jm/maps': { 'a.tmj': { src: 'data:image/png;base64,MAP', w: 64, h: 32 } } },
    structuredContent: { scene: 'scenes/level.scene.json', entities: [
      { where: 'entities[0]', name: 'Hero', x: 0, y: 0, w: 4, h: 4, components: { TransformComponent: { scale: [2, 2] }, SpriteComponent: {} } },
      map('Map', { TransformComponent: { position: [10, 20, 0] }, TileMapComponent: { map: 'a.tmj' } }),
      map('Loose', { TileMapComponent: { map: 'a.tmj' } })] } });
  await settle();
  const drawn = find((e) => e.attributes.class === 'sprite');
  assert.equal(drawn.length, 2);
  assert.deepEqual(['href', 'x', 'y', 'width', 'height'].map((k) => drawn[0].attributes[k]), ['data:image/png;base64,MAP', 10, -52, 64, 32]);
  assert.equal(drawn[1].tag, 'rect');  // the sprite, over the map
});
