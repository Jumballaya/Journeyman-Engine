import { Audio, Bus } from "./audio";
import { Input } from "./input";
import { PostEffect, Window } from "./render";
import { Save, Store } from "./state";
import { Session, StateFlag, StateNumber } from "./session";

export class SettingsOptions {
  musicVolume: f32 = 1;
  sfxVolume: f32 = 1;
  volumeStep: f64 = 0.1;
  fullscreenAction: string = "fullscreen";
  prefix: string = "";
}

// Saved audio/window preferences plus game-defined settings. Constructing this
// only declares settings; apply() restores them when the owning screen opens.
export class Settings extends Session {
  private options: SettingsOptions;
  private music: StateNumber<f32>;
  private sfx: StateNumber<f32>;
  private windowSetting: StateFlag;
  private effects: EffectSetting[] = [];
  constructor(options: SettingsOptions = new SettingsOptions()) {
    super(options.prefix, Save);
    this.options = options;
    this.music = new StateNumber<f32>(Save, options.prefix + "musicVolume", options.musicVolume, 0, 1, options.volumeStep);
    this.sfx = new StateNumber<f32>(Save, options.prefix + "sfxVolume", options.sfxVolume, 0, 1, options.volumeStep);
    this.windowSetting = new StateFlag(Save, options.prefix + "fullscreen");
    this.track(this.windowSetting);
    this.track(this.music);
    this.track(this.sfx);
  }
  get musicVolume(): f32 { return this.music.value; }
  set musicVolume(value: f32) { this.music.value = value; Audio.setVolume(Bus.Music, this.music.value); }
  get sfxVolume(): f32 { return this.sfx.value; }
  set sfxVolume(value: f32) { this.sfx.value = value; Audio.setVolume(Bus.Sfx, this.sfx.value); }
  get fullscreen(): bool { return Window.fullscreen; }
  set fullscreen(on: bool) {
    Window.fullscreen = on;
    this.windowSetting.value = on;
  }
  apply(): void {
    Audio.setVolume(Bus.Music, this.musicVolume);
    Audio.setVolume(Bus.Sfx, this.sfxVolume);
  }
  restoreWindow(): void {
    if (this.windowSetting.present && Window.fullscreen != this.windowSetting.value) Window.fullscreen = this.windowSetting.value;
  }
  effect(key: string, shader: string, initial: bool = true): EffectSetting {
    const setting = new EffectSetting(Save, this.options.prefix + key, initial, shader);
    this.effects.push(setting);
    this.track(setting);
    return setting;
  }
  // Each effect is created at most once per Settings instance (one per scene).
  open(): void {
    this.apply();
    this.restoreWindow();
    for (let i = 0; i < this.effects.length; i++) this.effects[i].open();
  }
  update(): void {
    if (this.options.fullscreenAction.length > 0 && Input.pressed(this.options.fullscreenAction)) this.fullscreen = !this.fullscreen;
    for (let i = 0; i < this.effects.length; i++) this.effects[i].refresh();
  }
}

export class EffectSetting extends StateFlag {
  private instance: PostEffect | null = null;
  private floats: Map<string, f32> = new Map<string, f32>();
  private applied: bool = false;
  constructor(store: Store, key: string, initial: bool, private shader: string) { super(store, key, initial); }
  get value(): bool { return this.store.getBool(this.key, this.initial); }
  set value(on: bool) { this.store.setBool(this.key, on); this.refresh(); }
  setFloat(name: string, value: f32): EffectSetting {
    this.floats.set(name, value);
    const effect = this.instance;
    if (effect !== null) effect.setFloat(name, value);
    return this;
  }
  open(): PostEffect {
    let effect = this.instance;
    if (effect === null) { effect = PostEffect.custom(this.shader); this.instance = effect; }
    const names = this.floats.keys();
    for (let i = 0; i < names.length; i++) effect.setFloat(names[i], this.floats.get(names[i]));
    this.applied = this.value;
    effect.enabled = this.applied;
    return effect;
  }
  refresh(): void {
    const effect = this.instance;
    if (effect !== null && this.applied != this.value) { this.applied = this.value; effect.enabled = this.applied; }
  }
}
