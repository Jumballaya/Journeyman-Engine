// Movement shared by enemies and items: sleeps until Pip is near, walks and
// turns at walls under gravity, and can be knocked out (flips and falls off-screen).
import { Entity, TileMap, World } from "@jm/runtime";
import { Body, GRAVITY } from "./body";

const WAKE_DISTANCE: f32 = 200;  // a bit more than half the 256px screen

export class Walker {
  readonly body: Body;
  readonly map: TileMap = TileMap.find("map");
  direction: f32 = -1;
  private awake: bool = false;
  private knocked: bool = false;

  // `artFacesLeft`: whether the unflipped sprite looks left.
  constructor(readonly me: Entity, halfW: f32, halfH: f32, public speed: f32, readonly artFacesLeft: bool = true) {
    this.body = new Body(halfW, halfH);
    this.body.x = me.transform.x;
    this.body.y = me.transform.y;
  }

  get knockedOut(): bool { return this.knocked; }

  // Flips over and falls out of the level, no longer touching anything.
  knockOut(): void {
    this.knocked = true;
    this.body.vy = 220;
    this.me.collider.collisionLayer = 0;
    this.me.collider.collisionMask = 0;
    this.me.removeTag("enemy");
    this.me.transform.scaleY = -this.me.transform.scaleY;
  }

  // Moves one frame; false while asleep.
  update(dt: f32): bool {
    if (!this.awake) {
      const player = World.find("player");
      if (player.isNone || Mathf.abs(player.transform.x - this.body.x) > WAKE_DISTANCE) return false;
      this.awake = true;
    }
    if (this.knocked) {
      this.body.vy -= GRAVITY * dt;
      this.body.x += this.body.vx * dt;
      this.body.y += this.body.vy * dt;
    } else {
      this.body.vx = this.direction * this.speed;
      this.body.fall(this.map, dt);
      if (this.body.hitWall) this.direction = -this.direction;
    }
    this.me.transform.setPosition(this.body.x, this.body.y);
    const facingLeft = this.direction < 0;
    const sx = Mathf.abs(this.me.transform.scaleX);
    this.me.transform.scaleX = facingLeft == this.artFacesLeft ? sx : -sx;
    if (this.body.y < -64) this.me.destroy();  // fell out of the level
    return true;
  }
}
