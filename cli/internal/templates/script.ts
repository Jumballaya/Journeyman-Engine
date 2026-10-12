// Runs on an entity with a ScriptComponent. Top-level code runs once, when the
// entity spawns; module variables are this entity's state. API: jm docs scripting.
import { Entity, Input, Params, self } from "@jm/runtime";

const me = self();
// Params are set on the entity's ScriptComponent (the editor's Inspector).
const speed = <f32>Params.number("speed", 200);

// Called every frame; dt is in seconds. The arrow keys or WASD move it.
export function onUpdate(dt: f32): void {
  me.transform.x += Input.axis("left", "right") * speed * dt;
  me.transform.y += Input.axis("down", "up") * speed * dt;
}

// Called when this entity's collider starts touching another (optional).
// Also: onOverlap (every frame), onOverlapEnd, onLanded, onLeftGround, onDestroy.
export function onOverlapStart(other: Entity): void {
}
