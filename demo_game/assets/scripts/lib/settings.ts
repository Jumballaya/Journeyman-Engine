// Player settings, saved across runs, and the screen-wide things they control.
import { Audio, Bus, Input, PostEffect, Save, Window } from "@jm/runtime";

export class Settings {
  static get musicVolume(): f32 { return <f32>Save.getNumber("musicVolume", 0.6); }
  static set musicVolume(v: f32) { Save.setNumber("musicVolume", tenths(v)); Settings.apply(); }
  static get sfxVolume(): f32 { return <f32>Save.getNumber("sfxVolume", 0.8); }
  static set sfxVolume(v: f32) { Save.setNumber("sfxVolume", tenths(v)); Settings.apply(); }
  static get crt(): bool { return Save.getBool("crt", true); }
  static set crt(on: bool) { Save.setBool("crt", on); }

  static get fullscreen(): bool { return Window.fullscreen; }
  static set fullscreen(on: bool) {
    Window.fullscreen = on;
    Save.setBool("fullscreen", on);
  }

  // Applies the saved volumes; call at startup.
  static apply(): void {
    Audio.setVolume(Bus.Music, Settings.musicVolume);
    Audio.setVolume(Bus.Sfx, Settings.sfxVolume);
  }

  // Re-enters fullscreen if the player left the game in fullscreen.
  static restoreWindow(): void {
    if (Save.getBool("fullscreen") && !Window.fullscreen) Window.fullscreen = true;
  }
}

function tenths(v: f32): f64 {
  return Math.round(Math.min(1, Math.max(0, v)) * 10) / 10;
}

// Every screen gets the CRT filter, disabled when the setting is off, so the
// options menu can toggle it live.
export function addCrt(): PostEffect {
  const crt = PostEffect.custom("crt").setFloat("u_strength", 1);
  crt.enabled = Settings.crt;
  return crt;
}

// Keys that work on every screen. Call from each screen's onUpdate.
export function handleGlobalKeys(): void {
  if (Input.pressed("fullscreen")) Settings.fullscreen = !Settings.fullscreen;
}
