// How a battle looks: a sprite per fighter (heroes on the right, enemies on
// the left), lunges, hit flashes, knockouts, spell effects, and floating
// numbers (popup entities with world text).
import { Camera, Entity, Overrides, Vec2, spawn } from "@jm/runtime";
import { Fighter } from "./battle";

const ATLAS = "assets/atlases/sprites.atlas.json#";
const HERO_X: f32 = 96;
const HERO_Y: f32[] = [72, 40, 8];
const FOE_SPOTS: f32[] = [-80, 52, -112, 16, -56, 4, -116, 76];  // x, y pairs

// A fighter's sprite and where it rests.
class Actor {
  shadow: Entity = Entity.NONE;
  flash: f32 = 0;    // seconds of hit flash left
  fade: f32 = -1;    // seconds into a knocked-out enemy's fade, or -1
  constructor(readonly fighter: Fighter, readonly sprite: Entity, readonly homeX: f32, readonly homeY: f32) {}
}


export class Stage {
  private actors: Actor[] = [];
  private screen: Vec2 = new Vec2();

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
      const x: f32 = f.boss ? -70 : FOE_SPOTS[i * 2];
      const y: f32 = f.boss ? 40 : FOE_SPOTS[i * 2 + 1];
      const foe = new Actor(f, spawn("foe_" + f.sprite, x, y), x, y);
      if (f.sprite != "wisp") {  // wisps float
        const size = foe.sprite.transform;  // scale is half the sprite's size
        foe.shadow = spawn("ground_shadow", x, y - size.scaleY + 1,
                           new Overrides().scale(size.scaleX * 0.7, f.boss ? 6 : 3));
      }
      this.actors.push(foe);
    }
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
    if (text.length > 0) {
      spawn("popup", a.sprite.transform.x, a.sprite.transform.y + 12,
            new Overrides().text("TextComponent", "text", text).text("TextComponent", "color", color));
    }
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

  // Updates flashes and knockouts; call every frame.
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
        else if (a.flash <= 0 && a.sprite.sprite.animation == "hurt") this.pose(a, "idle");
      }
    }
  }

  // Where a fighter is on screen (UI pixels), for the target cursor.
  screenX(f: Fighter): f32 { return Camera.toScreen(this.actor(f).sprite.transform.x, 0, this.screen).x; }
  screenY(f: Fighter): f32 { return Camera.toScreen(0, this.actor(f).sprite.transform.y, this.screen).y; }

  private pose(a: Actor, pose: string): void {
    a.sprite.sprite.play(pose);
    a.sprite.transform.scaleX = pose == "ko" ? -16 : -12;  // the lying-down frame is 32 wide, not 24
  }


  private actor(f: Fighter): Actor {
    for (let i = 0; i < this.actors.length; i++) if (this.actors[i].fighter === f) return this.actors[i];
    return this.actors[0];
  }
}
