export namespace console {
  export function log(...messages: string[]): void {
    const message = messages.join("");
    const utf8 = String.UTF8.encode(message, true);
    const view = Uint8Array.wrap(utf8);
    _log(view.dataStart, view.length - 1);
  }
}
