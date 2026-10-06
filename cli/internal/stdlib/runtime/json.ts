// JSON values for data files and game state: Json.parse(text), then read with
// get/at and number/text/bool, which fall back instead of failing.

export enum JsonKind { Null, Bool, Number, String, Array, Object }

export class JsonValue {
  kind: JsonKind = JsonKind.Null;
  private num: f64 = 0;
  private str: string = "";
  private items: JsonValue[] = [];
  private fields: Map<string, JsonValue> = new Map();
  private order: string[] = [];  // object keys, first-set first

  static number(value: f64): JsonValue { const v = new JsonValue(); v.kind = JsonKind.Number; v.num = value; return v; }
  static bool(value: bool): JsonValue { const v = new JsonValue(); v.kind = JsonKind.Bool; v.num = value ? 1 : 0; return v; }
  static string(value: string): JsonValue { const v = new JsonValue(); v.kind = JsonKind.String; v.str = value; return v; }
  static array(): JsonValue { const v = new JsonValue(); v.kind = JsonKind.Array; return v; }
  static object(): JsonValue { const v = new JsonValue(); v.kind = JsonKind.Object; return v; }

  static strings(values: string[]): JsonValue {
    const v = JsonValue.array();
    for (let i = 0; i < values.length; i++) v.push(JsonValue.string(values[i]));
    return v;
  }
  static numbers(values: f64[]): JsonValue {
    const v = JsonValue.array();
    for (let i = 0; i < values.length; i++) v.push(JsonValue.number(values[i]));
    return v;
  }

  get isNull(): bool { return this.kind == JsonKind.Null; }

  // An object's field; a null value if missing or not an object.
  get(key: string): JsonValue {
    return this.kind == JsonKind.Object && this.fields.has(key) ? this.fields.get(key) : new JsonValue();
  }
  has(key: string): bool { return this.kind == JsonKind.Object && this.fields.has(key); }
  keys(): string[] { return this.order.slice(); }

  // An array's element; a null value if out of range or not an array.
  at(index: i32): JsonValue {
    return this.kind == JsonKind.Array && index >= 0 && index < this.items.length ? this.items[index] : new JsonValue();
  }
  // Elements of an array, fields of an object, otherwise 0.
  get length(): i32 {
    return this.kind == JsonKind.Array ? this.items.length : this.kind == JsonKind.Object ? this.order.length : 0;
  }

  number(fallback: f64 = 0): f64 { return this.kind == JsonKind.Number || this.kind == JsonKind.Bool ? this.num : fallback; }
  int(fallback: i32 = 0): i32 { return this.kind == JsonKind.Number ? <i32>this.num : fallback; }
  text(fallback: string = ""): string { return this.kind == JsonKind.String ? this.str : fallback; }
  bool(fallback: bool = false): bool { return this.kind == JsonKind.Bool || this.kind == JsonKind.Number ? this.num != 0 : fallback; }

  // An array's string / number elements (others skipped).
  strings(): string[] {
    const out = new Array<string>();
    for (let i = 0; i < this.items.length; i++) if (this.items[i].kind == JsonKind.String) out.push(this.items[i].str);
    return out;
  }
  numbers(): f64[] {
    const out = new Array<f64>();
    for (let i = 0; i < this.items.length; i++) if (this.items[i].kind == JsonKind.Number) out.push(this.items[i].num);
    return out;
  }

  // Building: set turns a null value into an object, push into an array.
  set(key: string, value: JsonValue): JsonValue {
    if (this.kind == JsonKind.Null) this.kind = JsonKind.Object;
    if (!this.fields.has(key)) this.order.push(key);
    this.fields.set(key, value);
    return this;
  }
  push(value: JsonValue): JsonValue {
    if (this.kind == JsonKind.Null) this.kind = JsonKind.Array;
    this.items.push(value);
    return this;
  }

  toString(): string {
    switch (this.kind) {
      case JsonKind.Bool: return this.num != 0 ? "true" : "false";
      case JsonKind.Number: return isFinite(this.num) ? numberText(this.num) : "null";
      case JsonKind.String: return jsonString(this.str);
      case JsonKind.Array: {
        const parts = new Array<string>();
        for (let i = 0; i < this.items.length; i++) parts.push(this.items[i].toString());
        return `[${parts.join(",")}]`;
      }
      case JsonKind.Object: {
        const parts = new Array<string>();
        for (let i = 0; i < this.order.length; i++) {
          parts.push(`${jsonString(this.order[i])}:${this.fields.get(this.order[i]).toString()}`);
        }
        return `{${parts.join(",")}}`;
      }
      default: return "null";
    }
  }
}

export class Json {
  // Malformed text gives a null value (and logs nothing): check isNull.
  static parse(text: string): JsonValue {
    const p = new Parser(text);
    const value = p.value();
    p.space();
    return p.failed || p.pos != text.length ? new JsonValue() : value;
  }
}

class Parser {
  pos: i32 = 0;
  failed: bool = false;
  constructor(private text: string) {}

  value(): JsonValue {
    this.space();
    if (this.pos >= this.text.length) return this.fail();
    const c = this.text.charCodeAt(this.pos);
    if (c == 0x7B) return this.object();
    if (c == 0x5B) return this.array();
    if (c == 0x22) return JsonValue.string(this.string());
    if (this.word("true")) return JsonValue.bool(true);
    if (this.word("false")) return JsonValue.bool(false);
    if (this.word("null")) return new JsonValue();
    return this.number();
  }

  space(): void {
    while (this.pos < this.text.length) {
      const c = this.text.charCodeAt(this.pos);
      if (c != 0x20 && c != 0x0A && c != 0x0D && c != 0x09) return;
      this.pos++;
    }
  }

  private object(): JsonValue {
    const out = JsonValue.object();
    this.pos++;  // {
    this.space();
    if (this.eat(0x7D)) return out;
    while (!this.failed) {
      this.space();
      if (this.pos >= this.text.length || this.text.charCodeAt(this.pos) != 0x22) return this.fail();
      const key = this.string();
      this.space();
      if (!this.eat(0x3A)) return this.fail();
      out.set(key, this.value());
      this.space();
      if (this.eat(0x7D)) return out;
      if (!this.eat(0x2C)) return this.fail();
    }
    return out;
  }

  private array(): JsonValue {
    const out = JsonValue.array();
    this.pos++;  // [
    this.space();
    if (this.eat(0x5D)) return out;
    while (!this.failed) {
      out.push(this.value());
      this.space();
      if (this.eat(0x5D)) return out;
      if (!this.eat(0x2C)) return this.fail();
    }
    return out;
  }

  private string(): string {
    this.pos++;  // opening quote
    let out = "";
    let start = this.pos;
    while (this.pos < this.text.length) {
      const c = this.text.charCodeAt(this.pos);
      if (c == 0x22) {
        out += this.text.substring(start, this.pos);
        this.pos++;
        return out;
      }
      if (c != 0x5C) {
        this.pos++;
        continue;
      }
      out += this.text.substring(start, this.pos);
      const e = this.pos + 1 < this.text.length ? this.text.charCodeAt(this.pos + 1) : 0;
      this.pos += 2;
      if (e == 0x6E) out += "\n";
      else if (e == 0x74) out += "\t";
      else if (e == 0x72) out += "\r";
      else if (e == 0x62) out += "\b";
      else if (e == 0x66) out += "\f";
      else if (e == 0x75) {
        out += String.fromCharCode(<i32>parseInt(this.text.substring(this.pos, this.pos + 4), 16));
        this.pos += 4;
      } else out += String.fromCharCode(e);  // \" \\ \/
      start = this.pos;
    }
    this.failed = true;
    return out;
  }

  private number(): JsonValue {
    const start = this.pos;
    while (this.pos < this.text.length) {
      const c = this.text.charCodeAt(this.pos);
      const numeric = (c >= 0x30 && c <= 0x39) || c == 0x2D || c == 0x2B || c == 0x2E || c == 0x65 || c == 0x45;
      if (!numeric) break;
      this.pos++;
    }
    if (this.pos == start) return this.fail();
    const n = parseFloat(this.text.substring(start, this.pos));
    return isNaN(n) ? this.fail() : JsonValue.number(n);
  }

  private word(w: string): bool {
    if (this.text.substr(this.pos, w.length) != w) return false;
    this.pos += w.length;
    return true;
  }

  private eat(c: i32): bool {
    if (this.pos < this.text.length && this.text.charCodeAt(this.pos) == c) {
      this.pos++;
      return true;
    }
    return false;
  }

  private fail(): JsonValue {
    this.failed = true;
    return new JsonValue();
  }
}

// Integers print without a fraction ("3", not "3.0").
function numberText(value: f64): string {
  return value == Math.floor(value) && Math.abs(value) < 1e15 ? (<i64>value).toString() : value.toString();
}

// JSON encoding shared by prefab overrides and JsonValue. Internal.
export function jsonString(value: string): string {
  let result = '"';
  for (let i = 0; i < value.length; i++) {
    const code = value.charCodeAt(i);
    if (code == 34) result += '\\"';
    else if (code == 92) result += '\\\\';
    else if (code < 32) {
      const hex = code.toString(16);
      result += '\\u' + '0'.repeat(4 - hex.length) + hex;
    } else result += value.charAt(i);
  }
  return result + '"';
}

export function jsonNumber(value: f64): string {
  assert(isFinite(value), "Prefab overrides require finite numbers");
  return value.toString();
}
