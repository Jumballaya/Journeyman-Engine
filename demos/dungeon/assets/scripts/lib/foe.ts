// What every enemy shares: health, sword hits with knockback and a flash,
// staying inside its room, and dying in a puff that may drop a heart or gem.
import { Entity, Random, Sound, self, spawn } from "@jm/runtime";
import { ROOM_W, ROOM_H, areaById } from "./areas";
import { Body } from "./body";
import { Session } from "./session";
import { TILE, TileMap } from "./tiles";

const KNOCKBACK_SPEED: f32 = 220;
const KNOCKBACK_SECONDS: f32 = 0.15;
const HIT_FLASH_SECONDS: f32 = 0.3;

export class Foe {
  readonly me: Entity = self();
  readonly map: TileMap = new TileMap(areaById(Session.area));
  readonly body: Body;
  private hurtTime: f32 = 0;
  private knockX: f32 = 0;
  private knockY: f32 = 0;
  private left: f32;
  private bottom: f32;

  // `flies`: moves over walls and water (still kept inside the room).
  constructor(public health: i32, halfSize: f32, readonly flies: bool = false) {
    this.body = new Body(halfSize, halfSize);
    this.body.x = this.me.transform.x;
    this.body.y = this.me.transform.y;
    this.left = <f32>(TileMap.roomX(this.body.x) * ROOM_W) * TILE;
    this.bottom = <f32>(TileMap.roomY(this.body.y) * ROOM_H) * TILE;
  }

  get dead(): bool { return this.health <= 0; }
  get stunned(): bool { return this.hurtTime > HIT_FLASH_SECONDS - KNOCKBACK_SECONDS; }

  // Takes a sword hit from `sword`, knocked away from it. True if it killed.
  hitBy(sword: Entity, sound: string = "hit"): bool {
    if (this.dead || this.hurtTime > 0) return false;
    this.health--;
    this.hurtTime = HIT_FLASH_SECONDS;
    const dx = this.body.x - sword.transform.x, dy = this.body.y - sword.transform.y;
    const len = Mathf.max(Mathf.sqrt(dx * dx + dy * dy), 0.001);
    this.knockX = dx / len * KNOCKBACK_SPEED;
    this.knockY = dy / len * KNOCKBACK_SPEED;
    new Sound(this.dead ? "defeat" : sound).play(0.6);
    if (this.dead) this.die();
    return this.dead;
  }

  // Moves by (dx, dy) unless knocked back, then applies the frame's effects.
  update(dt: f32, dx: f32, dy: f32): void {
    if (this.dead) return;
    this.hurtTime -= dt;
    if (this.stunned) {
      dx = this.knockX * dt;
      dy = this.knockY * dt;
    }
    if (this.flies) {
      this.body.x += dx;
      this.body.y += dy;
    } else {
      this.body.move(this.map, dx, dy);
    }
    const margin = this.body.halfW + TILE;  // stay off the room's walls
    const w = <f32>ROOM_W * TILE, h = <f32>ROOM_H * TILE;
    this.body.x = Mathf.max(this.left + margin, Mathf.min(this.left + w - margin, this.body.x));
    this.body.y = Mathf.max(this.bottom + margin, Mathf.min(this.bottom + h - margin, this.body.y));
    this.me.transform.setPosition(this.body.x, this.body.y);
    if (this.hurtTime > 0) this.me.sprite.setColor(1, 0.3, 0.3);
    else this.me.sprite.setColor(1, 1, 1);
  }

  private die(): void {
    spawn("poof", this.body.x, this.body.y);
    const roll = Random.range(0, 1);
    if (roll < 0.3) spawn("heart", this.body.x, this.body.y);
    else if (roll < 0.6) spawn("gem", this.body.x, this.body.y);
    this.me.destroy();
  }
}
