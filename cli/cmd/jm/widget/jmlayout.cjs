// jm's JSON layout, for tests: a stand-in for jm's json_edit (pages ask jm;
// internal/jsonfmt is the layout). Kept to jm's by scene.test's testdata check.
const jsonText = (v, depth = 0) => {
  const pad = "  ".repeat(depth + 1), end = "  ".repeat(depth);
  if (Array.isArray(v)) {
    if (!v.length) return "[]";
    if (v.every((x) => x === null || typeof x !== "object")) {
      const line = "[" + v.map((x) => jsonText(x)).join(", ") + "]";
      if (line.length <= 72) return line;
    }
    return "[\n" + v.map((x) => pad + jsonText(x, depth + 1)).join(",\n") + "\n" + end + "]";
  }
  if (v && typeof v === "object") {
    const keys = Object.keys(v);
    if (!keys.length) return "{}";
    return "{\n" + keys.map((k) => pad + JSON.stringify(k) + ": " + jsonText(v[k], depth + 1)).join(",\n") + "\n" + end + "}";
  }
  if (typeof v === "number") {
    const n = Math.abs(v) < 1e9 ? Math.round(v * 1e6) / 1e6 : v;
    return Math.abs(n) >= 1e15 ? n.toExponential() : String(n);  // nlohmann's exponent past 1e15
  }
  return JSON.stringify(v);
};


// applied is doc with json_edit's edits made.
const applied = (doc, edits) => {
  for (const e of edits || []) {
    if (!e.path.length) { doc = e.value; continue; }
    const at = e.path.slice(0, -1).reduce((v, k) => v[k], doc), last = e.path.at(-1);
    if (e.remove) { if (Array.isArray(at)) at.splice(last, 1); else delete at[last]; } else at[last] = e.value;
  }
  return doc;
};

// jsonEdit answers json_edit as jm does.
const jsonEdit = ({ text, edits }) => ({ structuredContent: { text: jsonText(applied(JSON.parse(text), edits)) + '\n' } });

module.exports = { jsonText, jsonEdit };
