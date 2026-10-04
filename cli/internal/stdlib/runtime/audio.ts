import {
  __jmSoundPlay, __jmSoundStop, __jmSoundFadeOut, __jmSoundSetGain, __jmAudioSetBusVolume, __jmAudioStopAll,
} from "./env";
import { utf8 } from "./util";

// Mix groups. Each bus volume is multiplied by Master.
export enum Bus {
  Master = 0,
  Music = 1,
  Sfx = 2,
}

// A sound asset by file name ("laser" or "laser.wav") or path. play() can be
// called repeatedly; stop/fadeOut/gain control the latest playback.
export class Sound {
  private readonly name: Uint8Array;
  private playing: u32 = 0;

  constructor(name: string, readonly bus: Bus = Bus.Sfx) {
    this.name = utf8(name);
  }

  play(gain: f32 = 1, loop: bool = false): void {
    this.playing = __jmSoundPlay(this.name.dataStart, this.name.length, gain, loop, <i32>this.bus);
  }
  stop(): void { __jmSoundStop(this.playing); }
  fadeOut(seconds: f32): void { __jmSoundFadeOut(this.playing, seconds); }
  set gain(gain: f32) { __jmSoundSetGain(this.playing, gain); }
}

// Music that loops on the Music bus: new Music("theme").play().
export class Music extends Sound {
  constructor(name: string) { super(name, Bus.Music); }
  play(gain: f32 = 1, loop: bool = true): void { super.play(gain, loop); }
}

export class Audio {
  // 0..1, e.g. to apply saved settings.
  static setVolume(bus: Bus, volume: f32): void { __jmAudioSetBusVolume(<i32>bus, volume); }
  // Fades out everything playing (0 = cut).
  static stopAll(fadeSeconds: f32 = 0): void { __jmAudioStopAll(fadeSeconds); }
}
