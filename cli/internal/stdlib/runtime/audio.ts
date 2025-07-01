export namespace audio {
  export function playSound(name: string): void {
    const utf8 = String.UTF8.encode(name, true);
    const view = Uint8Array.wrap(utf8);
    playSoundNative(view.dataStart, view.length - 1);
  }
}
