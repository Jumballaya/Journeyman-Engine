
class Sound {
  private name: string;
  private id: u32 = 0;
  private _gain: f32 = 1.0;

  constructor(name: string) {
    this.name = name;
  }

  public play(gain: f32 = 1.0, looping: boolean = false): void {
    const utf8 = String.UTF8.encode(this.name, true);
    const view = Uint8Array.wrap(utf8);
    this.id = __jmPlaySound(<i32>view.dataStart, view.length - 1, gain, looping ? 1 : 0);
  }

  public stop(): void {
    if (this.id !== 0) {
      __jmStopSound(this.id);
    }
  }

  public fadeOut(durationInSeconds: f32): void {
    if (this.id !== 0) {
      __jmFadeOutSound(this.id, durationInSeconds);
    }
  }

  public set gain(gain: f32) {
    if (this.id !== 0) {
      this._gain = gain;
      __jmSetGainSound(this.id, gain);
    }
  }
};