// Studio in a fake MCP Apps host and DOM, for the Studio's tests (each tab's
// too). The page as jm serves it (mcp_studio.go): bridge, kit, then every
// widget/studio/*.js in name order.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const read = (name) => fs.readFileSync(path.join(__dirname, name), 'utf8');
const tabs = fs.readdirSync(path.join(__dirname, 'studio')).filter((f) => f.endsWith('.js')).sort().map((f) => read('studio/' + f));
const html = read('studio.html').replace('/*__JM_BRIDGE__*/', read('bridge.js'))
  .replace('/*__JM_STUDIO__*/', [read('kit.js'), ...tabs].join('\n'));
const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]).join('\n');

class Element {
  constructor(tag) {
    const props = {};
    Object.assign(this, { tag, children: [], attributes: {}, listeners: {}, dataset: {}, nodeType: 1,
      style: { setProperty: (k, v) => { props[k] = v; }, props } });
  }
  append(...kids) { this.children.push(...kids); }
  replaceChildren(...kids) { this.children = kids; }
  setAttribute(k, v) { this.attributes[k] = v; }
  getAttribute(k) { return this.attributes[k]; }
  addEventListener(name, fn) { (this.listeners[name] = this.listeners[name] || []).push(fn); }
  fire(name, event = {}) { for (const fn of this.listeners[name] || []) fn({ target: this, preventDefault() {}, stopPropagation() {}, ...event }); }
  focus() { doc.activeElement = this; }
  get text() { return this.children.map((c) => (typeof c === 'string' ? c : c.text)).join(' '); }
  get disabled() { return 'disabled' in this.attributes; }
  all(pred) { return [...(pred(this) ? [this] : []), ...this.children.flatMap((c) => (c.nodeType ? c.all(pred) : []))]; }
}
let doc;
const settle = async (n = 3) => { for (let i = 0; i < n; i++) await new Promise((resolve) => setImmediate(resolve)); };
const plain = (v) => JSON.parse(JSON.stringify(v));

// answers: tool name → result (or fn(args) → result); context: the host context
// at initialize; input: the tool's arguments; widgetState: what the host kept;
// refuseContext: the host refuses model context; caps: more host capabilities.
async function studio(result, { answers = {}, context = {}, input = null, widgetState = null, refuseContext = false, caps = {} } = {}) {
  const app = new Element('app');
  const root = new Element('html');
  const keys = [];
  doc = {
    createElement: (tag) => new Element(tag), createElementNS: (_, tag) => new Element(tag), getElementById: () => app,
    querySelector: (sel) => {
      const key = /^\[data-key="(.*)"\]$/.exec(sel);
      return key ? app.all((e) => e.attributes['data-key'] === key[1])[0] : app.all((e) => e.tag === 'main')[0];
    },
    documentElement: root, head: new Element('head'), body: { scrollHeight: 500 }, activeElement: null,
    addEventListener: (name, fn) => keys.push(fn),
  };
  const listeners = {};
  const sent = [];
  const parent = {
    postMessage(m) {
      sent.push(m);
      const reply = (r) => setImmediate(() => listeners.message({ source: parent, data: { jsonrpc: '2.0', id: m.id, result: r } }));
      if (m.method === 'ui/initialize') reply({ hostCapabilities: { updateModelContext: { text: {} }, ...caps }, hostContext: { displayMode: 'fullscreen', ...context } });
      else if (m.method === 'tools/call') {
        const a = answers[m.params.name];
        reply(typeof a === 'function' ? a(m.params.arguments) : a || (m.params.name === 'games' ? result : { content: [{ type: 'text', text: 'ok' }] }));
      } else if (refuseContext && m.method === 'ui/update-model-context') {
        setImmediate(() => listeners.message({ source: parent, data: { jsonrpc: '2.0', id: m.id, error: { code: -1, message: 'no' } } }));
      } else if (m.id !== undefined) reply({});
    },
  };
  let kept = widgetState;
  const window = { parent, addEventListener(name, fn) { listeners[name] = fn; },
                   openai: widgetState ? { widgetState, setWidgetState(s) { kept = s; } } : undefined };
  vm.runInNewContext(script, {
    window, document: doc, setTimeout, Date, URLSearchParams, console, TextDecoder, atob,
    CSS: { escape: (s) => s }, navigator: { platform: 'MacIntel' },
    matchMedia: () => ({ matches: false, addEventListener() {} }),
  });
  await settle();
  const notify = async (method, params) => { listeners.message({ source: parent, data: { jsonrpc: '2.0', method, params } }); await settle(); };
  if (input) await notify('ui/notifications/tool-input', { arguments: input });
  await notify('ui/notifications/tool-result', result);
  const find = (pred) => app.all(pred);
  return {
    app, root, sent, find, notify, settle,
    button: (label) => find((e) => e.tag === 'button' && e.text.trim() === label)[0],
    tab: (title) => find((e) => e.attributes.role === 'tab' && e.text.trim() === title)[0],
    current: () => find((e) => e.attributes.role === 'tab' && e.attributes['aria-selected'] === 'true')[0].text.trim(),
    calls: () => sent.filter((m) => m.method === 'tools/call').map((m) => plain(m.params)),
    contexts: () => sent.filter((m) => m.method === 'ui/update-model-context').map((m) => plain(m.params)),
    widgetState: () => kept,  // what the page saved last (a host given widgetState keeps it)
    key: async (event) => { for (const fn of keys) fn({ preventDefault() {}, ...event }); await settle(); },
  };
}

module.exports = { studio, settle, plain };
