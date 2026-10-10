import { Entity, GameState, Input, MapObject, TileMap, World, spawn } from "@jm/runtime";
import { Launch, podAim } from "./lib/pods";

// Owns capture and flight until landing; normal movement resumes afterwards.
export class PodChain {
  private markers: MapObject[] = TileMap.find("Map").objects("pod");
  private inside: i32 = -1;
  private last: i32 = -1;
  private cooldown: f32 = 0;
  private seconds: f32 = 0;
  private flying: bool = false;

  constructor() {
    GameState.setNumber("pod", 0);
    GameState.setNumber("podFlying", 0);
    GameState.setNumber("podsLaunched", 0);
  }

  reset(hero: Entity, gravity: f32): void {
    this.inside = this.last = -1;
    this.flying = false;
    this.cooldown = 0;
    hero.sprite.alpha = 1;
    hero.velocity.setAcceleration(0, gravity);
    GameState.setNumber("pod", 0);
    GameState.setNumber("podFlying", 0);
  }

  tick(hero: Entity, gravity: f32, dt: f32): bool {
    this.seconds += dt;
    this.cooldown = Mathf.max(0, this.cooldown - dt);
    if (this.flying && hero.velocity.onGround) {
      this.flying = false;
      GameState.setNumber("podFlying", 0);
    }
    const controlled = this.inside >= 0;
    for (let i = 0; i < this.markers.length; i++) {
      const marker = this.markers[i];
      const angle = podAim(<f32>marker.properties.get("angle").number(45),
        marker.properties.get("rotate").bool(), this.seconds);
      const pod = World.find("pod-" + i.toString());
      pod.transform.rotation = angle * Mathf.PI / 180;
      const active = i == this.inside;
      if (active != pod.data.getBool("active")) {
        pod.sprite.setTexture(active ? "assets/textures/pod_active.png" : "assets/textures/pod_closed.png");
        pod.data.setBool("active", active);
      }
      if (active) {
        hero.transform.setPosition(marker.x, marker.y);
        hero.sprite.alpha = 0;
        GameState.setNumber("podAngle", angle);
        if (Input.justPressed("jump")) {
          const velocity = new Launch(angle, <f32>marker.properties.get("speed").number(1250));
          hero.velocity.set(velocity.x, velocity.y);
          hero.velocity.setAcceleration(0, gravity);
          hero.sprite.alpha = 1;
          this.last = i;
          this.inside = -1;
          this.cooldown = 0.3;
          this.flying = true;
          const burst = spawn("launch", marker.x, marker.y);
          burst.particles.angle = angle + 180;
          GameState.setNumber("pod", 0);
          GameState.setNumber("podFlying", 1);
          GameState.add("podsLaunched", 1);
        }
      }
    }
    if (controlled) return true;
    for (let i = 0; i < this.markers.length; i++) {
      if (i == this.last && this.cooldown > 0) continue;
      const marker = this.markers[i];
      const dx = hero.transform.x - marker.x, dy = hero.transform.y - marker.y;
      if (dx * dx + dy * dy > 42 * 42) continue;
      this.inside = i;
      this.flying = false;
      hero.transform.setPosition(marker.x, marker.y);
      hero.velocity.set(0, 0);
      hero.velocity.setAcceleration(0, 0);
      hero.sprite.alpha = 0;
      GameState.setNumber("pod", i + 1);
      GameState.setNumber("podFlying", 0);
      return true;
    }
    return this.flying;
  }
}
