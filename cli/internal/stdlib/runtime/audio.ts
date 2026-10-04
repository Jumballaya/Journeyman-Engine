import {
  __jmPlaySound, __jmStopSound, __jmFadeOutSound, __jmSetGainSound,
  __jmAudioSetBusVolume, __jmAudioStopAll,
} from "./env";
import { utf8 } from "./util";

// Mix groups; volume per bus is multiplied by Master.
export enum Bus {
  Master = 0,
  Music = 1,
  Sfx = 2,
}

// A sound asset, named by asset path ("assets/sounds/laser.wav") or file name
// ("laser.wav"). One Sound can be played many times; play() returns control
// over the latest playback only.
export class Sound {
  private readonly nameBytes: Uint8Array;
  private id: u32 = 0;

  constructor(public readonly name: string, public readonly bus: Bus = Bus.Sfx) {
    this.nameBytes = utf8(name);
  }

  public play(gain: f32 = 1.0, looping: boolean = false): void {
    const n = this.nameBytes;
    this.id = __jmPlaySound(<i32>n.dataStart, n.length - 1, gain, looping ? 1 : 0, <i32>this.bus);
  }

  public stop(): void {
    if (this.id !== 0) __jmStopSound(this.id);
  }

  public fadeOut(durationInSeconds: f32): void {
    if (this.id !== 0) __jmFadeOutSound(this.id, durationInSeconds);
  }

  public set gain(gain: f32) {
    if (this.id !== 0) __jmSetGainSound(this.id, gain);
  }
}

export class Audio {
  // 0..1. Typical use: apply saved settings at startup.
  static setVolume(bus: Bus, volume: f32): void {
    __jmAudioSetBusVolume(<i32>bus, volume);
  }

  // Fades every playing sound (0 = cut immediately).
  static stopAll(fadeSeconds: f32 = 0): void {
    __jmAudioStopAll(fadeSeconds);
  }
}
