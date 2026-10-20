// Studio in a fake MCP Apps host and DOM, for the Studio's tests (each tab's
// too). The page as jm serves it (mcp_widgets.go, mcp_studio.go): bridge and
// kit, then every widget/studio/*.js in name order.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const read = (name) => fs.readFileSync(path.join(__dirname, name), 'utf8');
const tabs = fs.readdirSync(path.join(__dirname, 'studio')).filter((f) => f.endsWith('.js')).sort().map((f) => read('studio/' + f));
const html = read('studio.html').replace('/*__JM_BRIDGE__*/', read('bridge.js') + '\n' + read('kit.js'))
  .replace('/*__JM_STUDIO__*/', tabs.join('\n'));
const script = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]).join('\n');

const settle = async (n = 3) => { for (let i = 0; i < n; i++) await new Promise((resolve) => setImmediate(resolve)); };
const plain = (v) => JSON.parse(JSON.stringify(v));

// answers: tool name (or request method) → result (or fn(args) → result); context: the host context
// at initialize; input: the tool's arguments; widgetState: what the host kept;
// refuseContext: the host refuses model context; caps: more host capabilities;
// storage: the page's localStorage (a Map, kept across studio() calls); openai: more of window.openai (ChatGPT's extensions).
async function studio(result, { answers = {}, context = {}, input = null, widgetState = null, refuseContext = false, caps = {}, storage = new Map(), openai = null } = {}) {
  // Each Studio its own document: an earlier one's timers can't touch it.
  class Element {
    constructor(tag) {
      const props = {};
      Object.assign(this, { tag, children: [], attributes: {}, listeners: {}, dataset: {}, nodeType: 1,
        style: { setProperty: (k, v) => { props[k] = v; }, props } });
    }
    append(...kids) { this.children.push(...kids); }
    // As a browser does when the focused element goes: change, then blur.
    replaceChildren(...kids) {
      const at = doc && doc.activeElement, has = (list) => list.some((c) => c.nodeType && c.all((e) => e === at).length);
      const gone = at && has(this.children) && !has(kids);
      this.children = kids;
      if (gone) { doc.activeElement = null; at.fire('change', { target: { value: at.value ?? at.attributes.value } }); at.fire('blur'); }
    }
    setAttribute(k, v) { this.attributes[k] = v; }
    getAttribute(k) { return this.attributes[k]; }
    addEventListener(name, fn) { (this.listeners[name] = this.listeners[name] || []).push(fn); }
    fire(name, event = {}) { for (const fn of this.listeners[name] || []) fn({ target: this, preventDefault() {}, stopPropagation() {}, ...event }); }
    focus() { doc.activeElement = this; }
    get text() { return this.children.map((c) => (typeof c === 'string' ? c : c.text)).join(' '); }
    get disabled() { return 'disabled' in this.attributes; }
    contains(node) { return this.all((e) => e === node).length > 0; }
    all(pred) { return [...(pred(this) ? [this] : []), ...this.children.flatMap((c) => (c.nodeType ? c.all(pred) : []))]; }
  }
  let doc;
  const app = new Element('app');
  const root = new Element('html');
  const keys = [], changes = [];  // the document's keydown and selectionchange listeners
  // The selection: none, or { anchorNode, anchorOffset, focusNode, focusOffset } (select() sets it).
  let selected = null;
  const selection = {
    get isCollapsed() { return !selected; },
    get anchorNode() { return selected && selected.anchorNode; }, get anchorOffset() { return selected ? selected.anchorOffset : 0; },
    get focusNode() { return selected && selected.focusNode; }, get focusOffset() { return selected ? selected.focusOffset : 0; },
    setBaseAndExtent(anchorNode, anchorOffset, focusNode, focusOffset) { selected = { anchorNode, anchorOffset, focusNode, focusOffset }; },
  };
  doc = {
    createElement: (tag) => new Element(tag), createElementNS: (_, tag) => new Element(tag), getElementById: () => app,
    querySelector: (sel) => {
      const key = /^\[data-key="(.*)"\]$/.exec(sel);
      return key ? app.all((e) => e.attributes['data-key'] === key[1])[0] : app.all((e) => e.tag === 'main')[0];
    },
    documentElement: root, head: new Element('head'), body: { scrollHeight: 500 }, activeElement: null,
    addEventListener: (name, fn) => (name === 'selectionchange' ? changes : keys).push(fn),
  };
  const listeners = {};
  const sent = [];
  const parent = {
    postMessage(m) {
      sent.push(m);
      const reply = (r) => setImmediate(() => listeners.message({ source: parent, data: { jsonrpc: '2.0', id: m.id, result: r } }));
      if (m.method === 'ui/initialize') reply({ hostCapabilities: { updateModelContext: { text: {} }, ...caps }, hostContext: { displayMode: 'fullscreen', ...context } });
      else if (m.method === 'tools/call') {
        const a = answers[m.params.name], r = typeof a === 'function' ? a(m.params.arguments) : a || (m.params.name === 'games' ? result : { content: [{ type: 'text', text: 'ok' }] });
        if (r && r.rpcError) setImmediate(() => listeners.message({ source: parent, data: { jsonrpc: '2.0', id: m.id, error: { code: -1, message: r.rpcError } } }));  // the call itself fails
        else reply(r);
      } else if (refuseContext && m.method === 'ui/update-model-context') {
        setImmediate(() => listeners.message({ source: parent, data: { jsonrpc: '2.0', id: m.id, error: { code: -1, message: 'no' } } }));
      } else if (answers[m.method]) {  // other requests: resources/read, openai/resources/write
        const a = answers[m.method];
        reply(typeof a === 'function' ? a(m.params) : a);
      } else if (m.id !== undefined) reply({});
    },
  };
  let kept = widgetState;
  const window = { parent, addEventListener(name, fn) { listeners[name] = fn; },
                   openai: widgetState || openai ? { ...(widgetState ? { widgetState, setWidgetState(s) { kept = s; } } : {}), ...openai } : undefined };
  vm.runInNewContext(script, {
    window, document: doc, Date, queueMicrotask, getSelection: () => selection, setTimeout: (fn, ms) => { const t = setTimeout(fn, ms); t.unref(); return t; },  // a tab's polling doesn't keep a test alive
    URLSearchParams, console, TextDecoder, atob, btoa, Uint8Array,
    localStorage: { getItem: (k) => (storage.has(k) ? storage.get(k) : null), setItem: (k, v) => storage.set(k, String(v)) },
    CSS: { escape: (s) => s }, navigator: { platform: 'MacIntel' },
    matchMedia: () => ({ matches: false, addEventListener() {} }),
  });
  await settle();
  const notify = async (method, params) => { listeners.message({ source: parent, data: { jsonrpc: '2.0', method, params } }); await settle(); };
  if (input) await notify('ui/notifications/tool-input', { arguments: input });
  await notify('ui/notifications/tool-result', result);
  const find = (pred) => app.all(pred);
  return {
    app, root, sent, find, notify, settle, windowEvent: async (name) => { listeners[name](); await settle(); },
    button: (label) => find((e) => e.tag === 'button' && e.attributes.role !== 'tab' && e.text.trim() === label)[0],  // tabs: tab()
    tab: (title) => find((e) => e.attributes.role === 'tab' && e.text.trim() === title)[0],
    current: () => find((e) => e.attributes.role === 'tab' && e.attributes['aria-selected'] === 'true')[0].text.trim(),
    calls: () => sent.filter((m) => m.method === 'tools/call').map((m) => plain(m.params)),
    contexts: () => sent.filter((m) => m.method === 'ui/update-model-context').map((m) => plain(m.params)),
    widgetState: () => kept,  // what the page saved last (a host given widgetState keeps it)
    key: async (event) => { for (const fn of keys) fn({ preventDefault() {}, ...event }); await settle(); },
    // Selects from one node to another (null: none), as a person's drag does.
    select: async (from, to = from) => { selected = from && { anchorNode: from, anchorOffset: 0, focusNode: to, focusOffset: 1 }; for (const fn of changes) fn(); await settle(); },
    selection: () => selected,
  };
}

module.exports = { studio, settle, plain };
