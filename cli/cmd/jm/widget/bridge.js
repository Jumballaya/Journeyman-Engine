// The host bridge every jm widget is served with (jm inlines it). One host
// object for MCP Apps hosts (JSON-RPC over postMessage: Claude, ChatGPT's
// apps) and ChatGPT's window.openai, the extensions used where advertised.
//
// jmHost({ name, version, displayModes }) gives:
//   result      the tool's result: { structuredContent, _meta, content, isError }, or null
//   input       the tool's arguments (a file entrypoint's { file }), or null
//   theme, displayMode, displayModes, context (host context), deepLink
//   onChange(fn)          called whenever any of the above changes
//   callTool(name, args)  a tool's whole result
//   message(content, { send, target, instead })   to the chat; content: text or
//     blocks; instead: sent in its place by a host that takes no openai/message
//   updateContext({ content, structuredContent })   what the model sees next turn;
//     a block the person took out of the chat isn't put back until it changes
//   attached   the context the host holds from this page ({ content, structuredContent }),
//     null once the person removed it, undefined until the host says
//   text(text, { title, hidden, thumbnail }), image(dataURI, { title }),
//     link(uri, { name, title, mimeType })   content blocks (thumbnail: an image URL)
//   readResource(uri): { text, etag, writable }; writeResource(uri, text, ifMatch):
//     { outcome: "saved" | "conflict" | "too-large", etag, maxBytes } (a file entrypoint's file)
//   requestDisplayMode(mode), notifyHeight(px), widgetState, setWidgetState(s)
//   can(feature): "callTool", "message", "updateContext", "displayMode", or an
//     experimental host capability ("openai/message")
function jmHost(app) {
  "use strict";
  const openai = typeof window.openai === "object" && window.openai ? window.openai : null;
  const framed = !!window.parent && window.parent !== window;
  const listeners = [];
  const changed = () => { for (const fn of listeners) { try { fn(); } catch (e) { console.error(e); } } };
  const h = {
    result: null, input: null, theme: null, displayMode: "inline", displayModes: null, context: {}, deepLink: null, attached: undefined,
    widgetState: openai ? openai.widgetState : null,
    onChange: (fn) => { listeners.push(fn); },
  };
  // mcp: the MCP Apps connection, once the host answered ui/initialize.
  let mcp = null;
  const experimental = (name) => !!(mcp && mcp.caps.experimental && mcp.caps.experimental[name]);
  // window.openai until an MCP Apps host answers (ChatGPT's skybridge never does).
  const legacy = () => !!openai && !mcp;
  let next = 1;
  const waiting = new Map();
  const send = (msg) => window.parent.postMessage({ jsonrpc: "2.0", ...msg }, "*");
  const request = (method, params) => new Promise((resolve, reject) => {
    const id = next++;
    waiting.set(id, { resolve, reject });
    send({ id, method, params });
  });

  // A whole result, from either shape: ChatGPT's toolOutput is its structuredContent.
  const resultOf = (out, meta) => {
    if (out === null || out === undefined || (typeof out === "object" && !Object.keys(out).length)) return null;
    const whole = typeof out === "object" && ("structuredContent" in out || "content" in out || "isError" in out);
    const r = whole ? { ...out } : { structuredContent: out };
    r._meta = { ...(r._meta || {}), ...(meta || {}) };
    return r;
  };
  // Once an MCP Apps host answered, its notifications are the truth.
  // seen: the globals the result was made from (they change for theme and the like too).
  let seen = [];
  const fromOpenAI = () => {
    h.widgetState = openai.widgetState;
    if (!legacy()) return;
    if (seen[0] !== openai.toolOutput || seen[1] !== openai.toolResponseMetadata) {
      seen = [openai.toolOutput, openai.toolResponseMetadata];
      h.result = resultOf(openai.toolOutput, openai.toolResponseMetadata) || h.result;
    }
    if (openai.toolInput && Object.keys(openai.toolInput).length) h.input = openai.toolInput;
    if (openai.theme) h.theme = openai.theme;
    if (openai.displayMode) h.displayMode = openai.displayMode;
  };
  const applyContext = (c) => {
    if (!c) return;
    Object.assign(h.context, c);
    if (c.theme) h.theme = c.theme;
    if (c.displayMode) h.displayMode = c.displayMode;
    if (Array.isArray(c.availableDisplayModes)) h.displayModes = c.availableDisplayModes;
    if ("openai/deepLink" in c) h.deepLink = c["openai/deepLink"] ? c["openai/deepLink"].url : null;
    if ("openai/modelContext" in c) {
      const held = c["openai/modelContext"];
      const notice = { before: shown(h.attached ? h.attached.content : []), held };
      if (updating.inFlight) updating.deferred.push(notice); else judge(notice);
      h.attached = held ? { content: held.content || [], structuredContent: held.structuredContent } : null;
    }
  };
  // removed: the visible blocks the person took out of the chat (as JSON).
  // updating: the updates the host took since none were in flight (whole, and
  // the updateIds it gave), and its notices meanwhile, judged once all settle.
  const removed = new Set(), updating = { inFlight: 0, taken: [], ids: new Set(), deferred: [] };
  const sorted = (k, v) => (v && typeof v === "object" && !Array.isArray(v) ? Object.fromEntries(Object.keys(v).sort().map((key) => [key, v[key]])) : v);
  const whole = (content, structuredContent) => JSON.stringify([content || [], structuredContent === undefined ? null : structuredContent], sorted);  // key order aside
  const shown = (content) => (content || []).filter((b) => !(b.annotations && (b.annotations.audience || []).every((a) => a === "assistant")))
    .map((b) => JSON.stringify(b));
  // A notice of what the host holds: the page's own update, or the person
  // took out what it lacks of what was attached before.
  const judge = ({ before, held }) => {
    const kept = shown(held ? held.content : []);
    const own = !!held && (updating.ids.has(held.updateId) || updating.taken.includes(whole(held.content, held.structuredContent)));
    if (!own) for (const b of before) if (!kept.includes(b)) removed.add(b);
  };
  const updateSettled = () => {
    if (--updating.inFlight) return;
    updating.deferred.forEach(judge);
    Object.assign(updating, { taken: [], ids: new Set(), deferred: [] });
  };

  // settled: the host answered ui/initialize (or refused it). ChatGPT's
  // skybridge never answers: there window.openai is the host after a second.
  let settle;
  const settled = new Promise((resolve) => { settle = resolve; });
  const ready = () => !framed || mcp ? Promise.resolve()
    : Promise.race([settled, ...(openai ? [new Promise((resolve) => setTimeout(resolve, 1000))] : [])]);
  if (framed) {
    window.addEventListener("message", (e) => {
      const m = e.data;
      if (e.source !== window.parent || !m || m.jsonrpc !== "2.0") return;
      if (m.method && m.id !== undefined) {
        // The host asks (ping, ui/resource-teardown): nothing to do but answer.
        const answers = m.method === "ping" || m.method === "ui/resource-teardown";
        send(answers ? { id: m.id, result: {} } : { id: m.id, error: { code: -32601, message: "not handled: " + m.method } });
      } else if (!m.method && waiting.has(m.id)) {
        const w = waiting.get(m.id);
        waiting.delete(m.id);
        if (m.error) w.reject(new Error(m.error.message)); else w.resolve(m.result);
      } else if (m.method === "ui/notifications/tool-result") {
        h.result = resultOf(m.params);
        changed();
      } else if (m.method === "ui/notifications/tool-input") {
        h.input = (m.params && m.params.arguments) || {};
        changed();
      } else if (m.method === "ui/notifications/host-context-changed") {
        applyContext(m.params);
        changed();
      }
    });
    request("ui/initialize", { protocolVersion: "2026-01-26", appInfo: { name: app.name, version: app.version },
                               appCapabilities: { availableDisplayModes: app.displayModes || ["inline", "fullscreen"] } })
      .then((r) => { mcp = { caps: (r && r.hostCapabilities) || {} }; applyContext(r && r.hostContext); })
      .catch(() => {})  // a host that won't say its context still gets told we're ready
      .then(() => { settle(); send({ method: "ui/notifications/initialized" }); changed(); });
  }
  if (openai) {
    fromOpenAI();
    window.addEventListener("openai:set_globals", () => { fromOpenAI(); changed(); });
  }

  // Framed, the JSON-RPC methods are tried: a host that refuses one rejects it.
  h.can = (feature) => {
    switch (feature) {
      case "callTool": return !!(legacy() ? openai.callTool : framed);
      case "message": return !!(legacy() ? openai.sendFollowUpMessage : framed);
      case "updateContext": return !legacy() && framed;
      case "displayMode": return !!(legacy() ? openai.requestDisplayMode : framed);
      default: return experimental(feature);
    }
  };
  h.callTool = (name, args) => legacy()
    ? Promise.resolve(openai.callTool(name, args)).then((r) => resultOf(r) || {})
    : request("tools/call", { name, arguments: args });
  const blocks = (content) => typeof content === "string" ? [{ type: "text", text: content }] : content;
  // Waits for the host to say whether it takes openai/message: send and
  // target mean nothing to one that doesn't.
  h.message = (content, options) => ready().then(() => sendMessage(content, options));
  const sendMessage = (content, { send: sendNow = true, target = "active", instead } = {}) => {
    if (instead !== undefined && !experimental("openai/message")) content = instead;
    if (legacy()) {
      const prompt = blocks(content).filter((b) => b.type === "text").map((b) => b.text).join("\n\n");
      return Promise.resolve(openai.sendFollowUpMessage({ prompt }));
    }
    const params = { role: "user", content: blocks(content) };
    if (experimental("openai/message")) params._meta = { "openai/message": { target, send: sendNow } };
    return request("ui/message", params);
  };
  // false: the host doesn't take model context, or refused it.
  // Sent once the host is initialized: a host still starting up may drop it.
  h.updateContext = ({ content = [], structuredContent } = {}) => ready().then(() => {
    if (!h.can("updateContext")) return false;
    const params = { content: content.filter((b) => !removed.has(JSON.stringify(b))) };
    if (structuredContent) params.structuredContent = structuredContent;
    updating.inFlight++;
    return request("ui/update-model-context", params).then((r) => {
      const id = r && ((r._meta && r._meta["openai/modelContext"] && r._meta["openai/modelContext"].updateId) || r.updateId);
      if (id) updating.ids.add(id);
      updating.taken.push(whole(params.content, params.structuredContent));
      h.attached = { content: params.content, structuredContent };
      updateSettled();
      return true;
    }, () => { updateSettled(); return false; });
  });
  h.readResource = (uri) => request("resources/read", { uri, _meta: { "openai/resource": { representation: "text" } } })
    .then((r) => {
      const c = (r && r.contents && r.contents[0]) || {};
      const meta = (c._meta && c._meta["openai/resource"]) || {};
      const text = typeof c.text === "string" ? c.text
        : new TextDecoder().decode(Uint8Array.from(atob(c.blob || ""), (ch) => ch.charCodeAt(0)));
      return { text, etag: meta.etag, writable: !!meta.writable };
    });
  h.writeResource = (uri, text, ifMatch) => request("openai/resources/write", { uri, text, ...(ifMatch ? { ifMatch } : {}) });
  // hidden: for the model only, not shown as an attachment.
  h.text = (text, { title, hidden, thumbnail } = {}) => {
    const meta = { ...(title ? { "openai/title": title } : {}), ...(thumbnail ? { "openai/thumbnail": { src: thumbnail } } : {}) };
    return { type: "text", text, ...(Object.keys(meta).length ? { _meta: meta } : {}), ...(hidden ? { annotations: { audience: ["assistant"] } } : {}) };
  };
  h.link = (uri, { name, title, mimeType } = {}) => ({ type: "resource_link", uri, name: name || uri.split("/").pop(),
    ...(title ? { title } : {}), ...(mimeType ? { mimeType } : {}) });
  h.image = (src, { title } = {}) => {
    const [, mimeType, data] = /^data:([^;,]+);base64,(.*)$/.exec(src) || [];
    return { type: "image", mimeType, data, ...(title ? { _meta: { "openai/title": title } } : {}) };
  };
  h.requestDisplayMode = (mode) => {
    const asked = legacy() ? Promise.resolve(openai.requestDisplayMode({ mode })) : request("ui/request-display-mode", { mode });
    return asked.then((r) => { if (r && r.mode) { h.displayMode = r.mode; changed(); } });
  };
  h.notifyHeight = (height) => {
    if (legacy()) { if (openai.notifyIntrinsicHeight) openai.notifyIntrinsicHeight(height); }
    else if (framed) send({ method: "ui/notifications/size-changed", params: { height } });
  };
  h.setWidgetState = (s) => { h.widgetState = s; if (openai && openai.setWidgetState) openai.setWidgetState(s); };
  return h;
}
