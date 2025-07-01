namespace console {
  function log(...messages: string[]): void {
    const message = messages.join("");
    const utf8 = String.UTF8.encode(message, true);
    const view = Uint8Array.wrap(utf8);
    jmLog(view.dataStart, view.length - 1);
  }
}
