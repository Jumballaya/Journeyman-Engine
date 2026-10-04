// Internal JSON encoding shared by typed prefab overrides.
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
