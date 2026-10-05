// How a battle looks: a sprite per fighter (heroes on the right, enemies on
// the left), lunges, hit flashes, knockouts, spell effects, and floating
// numbers (UI elements #pop-0..#pop-7 placed over the sprites).
import { Entity, Overrides, UI, spawn } from "@jm/runtime";
import { Fighter } from "./battle";

const ATLAS = "assets/atlases/sprites.atlas.json#";
const POPUPS = 8;
const POPUP_SECONDS: f32 = 0.9;
const HALF_W: f32 = 160;  // world (0, 0) is the screen's center; UI pixels start top-left
const HALF_H: f32 = 120;
const HERO_X: f32 = 96;
const HERO_Y: f32[] = [72, 40, 8];
const FOE_SPOTS: f32[] = [-80, 52, -112, 16, -56, 4, -116, 76];  // x, y pairs

// A fighter's sprite and where it rests.
class Actor {
  shadow: Entity = Entity.NONE;
  flash: f32 = 0;    // seconds of hit flash left
  fade: f32 = -1;    // seconds into a knocked-out enemy's fade, or -1
  shown: string = "";
  constructor(readonly fighter: Fighter, readonly sprite: Entity, readonly homeX: f32, readonly homeY: f32) {}
}

class Popup {
  age: f32 = POPUP_SECONDS;
  x: f32 = 0;
  y: f32 = 0;
}

export class Stage {
  private actors: Actor[] = [];
  private popups: Popup[] = [];
  private nextPopup: i32 = 0;

  constructor(heroes: Fighter[], foes: Fighter[], lair: bool) {
    spawn("backdrop", 0, 36, new Overrides().texture(ATLAS + (lair ? "bg_lair" : "bg_forest")));
    for (let i = 0; i < heroes.length; i++) {
      const x = HERO_X + <f32>i * 12, y = HERO_Y[i];
      const hero = new Actor(heroes[i], spawn("hero_" + heroes[i].id, x, y), x, y);
      hero.shadow = spawn("ground_shadow", x, y - 15);
      this.actors.push(hero);
    }
    for (let i = 0; i < foes.length; i++) {
      const f = foes[i];
      const x: f32 = f.boss ? -88 : FOE_SPOTS[i * 2];
      const y: f32 = f.boss ? 40 : FOE_SPOTS[i * 2 + 1];
      const foe = new Actor(f, spawn("foe_" + f.sprite, x, y), x, y);
      if (f.sprite != "wisp") {  // wisps float
        foe.shadow = spawn("ground_shadow", x, y - (f.boss ? 34 : 11), new Overrides().scale(f.boss ? 40 : 14, f.boss ? 6 : 3));
      }
      this.actors.push(foe);
    }
    for (let i = 0; i < POPUPS; i++) this.popups.push(new Popup());
  }

  // Steps a fighter forward (true) or back to its place; casters raise
  // their weapon instead of swinging it.
  lunge(f: Fighter, out: bool, casting: bool = false): void {
    const a = this.actor(f);
    const dx: f32 = out ? (f.hero ? -20 : 16) : 0;
    this.moveTo(a, a.homeX + dx);
    if (f.hero) this.pose(a, out ? (casting ? "cast" : "attack") : "idle");
  }

  // A hero waiting for orders stands a little forward.
  ready(f: Fighter, on: bool): void {
    const a = this.actor(f);
    this.moveTo(a, a.homeX - (on ? 8 : 0));
  }

  private moveTo(a: Actor, x: f32): void {
    a.sprite.transform.x = x;
    if (!a.shadow.isNone) a.shadow.transform.x = x;
  }

  // Floats `text` over a fighter in `color` (CSS); a harmful hit also flashes it.
  show(f: Fighter, text: string, color: string, harmful: bool): void {
    const a = this.actor(f);
    if (text.length > 0) this.popup(a, text, color);
    if (!harmful) return;
    a.flash = 0.25;
    if (f.hero && f.alive) this.pose(a, "hurt");
  }

  // A spell or slash drawn over the fighter: slash, fire, ice, bolt or heal.
  effect(f: Fighter, kind: string): void {
    const a = this.actor(f);
    spawn("effect", a.sprite.transform.x, a.sprite.transform.y,
          new Overrides().text("SpriteAnimationComponent", "current", kind));
  }

  // Updates flashes, knockouts and popups; call every frame.
  update(dt: f32): void {
    for (let i = 0; i < this.actors.length; i++) {
      const a = this.actors[i];
      a.flash -= dt;
      const lit: f32 = a.flash > 0 && <i32>Mathf.floor(a.flash * 30) % 2 == 0 ? 0.4 : 1;
      if (!a.fighter.alive && !a.fighter.hero) {
        if (a.fade < 0) a.fade = 0;
        a.fade += dt;
        a.sprite.sprite.setColor(1, 0.4, 0.4, Mathf.max(0, 1 - a.fade * 2.5));
        if (!a.shadow.isNone) a.shadow.sprite.alpha = Mathf.max(0, 1 - a.fade * 2.5);
      } else {
        a.sprite.sprite.setColor(1, lit, lit, a.fighter.sleep > 0 ? 0.6 : 1);
      }
      if (a.fighter.hero) {
        if (!a.fighter.alive) this.pose(a, "ko");
        else if (a.flash <= 0 && a.shown == "hurt") this.pose(a, "idle");
      }
    }
    for (let i = 0; i < this.popups.length; i++) {
      const p = this.popups[i];
      if (p.age >= POPUP_SECONDS) continue;
      p.age += dt;
      const id = "pop-" + i.toString();
      UI.setStyle(id, "top", (<i32>(HALF_H - p.y - 18 - p.age * 16)).toString() + "px");
      UI.opacity(id, p.age < POPUP_SECONDS ? 1 - Mathf.max(0, p.age - 0.5) / 0.4 : 0);
    }
  }

  // Where a fighter is on screen (UI pixels), for the target cursor.
  screenX(f: Fighter): f32 { return this.actor(f).sprite.transform.x + HALF_W; }
  screenY(f: Fighter): f32 { return HALF_H - this.actor(f).sprite.transform.y; }

  private pose(a: Actor, pose: string): void {
    if (pose == a.shown) return;  // Sprite.play restarts; only switch on change
    a.shown = pose;
    a.sprite.sprite.play(pose);
    a.sprite.transform.scaleX = pose == "ko" ? -16 : -12;  // the lying-down frame is 32 wide, not 24
  }

  private popup(a: Actor, text: string, color: string): void {
    const i = this.nextPopup;
    this.nextPopup = (this.nextPopup + 1) % POPUPS;
    const p = this.popups[i];
    p.age = 0;
    p.x = a.sprite.transform.x;
    p.y = a.sprite.transform.y;
    const id = "pop-" + i.toString();
    UI.setText(id, text);
    UI.setStyle(id, "color", color);
    UI.setStyle(id, "left", (<i32>(p.x + HALF_W - 20)).toString() + "px");
    UI.setStyle(id, "top", (<i32>(HALF_H - p.y - 18)).toString() + "px");
    UI.opacity(id, 1);
  }

  private actor(f: Fighter): Actor {
    for (let i = 0; i < this.actors.length; i++) if (this.actors[i].fighter === f) return this.actors[i];
    return this.actors[0];
  }
}
