// The mouse, for a game that is also played with keys: where the pointer is
// in the world, what it's over, and clicks. Call Pointer.update() once a frame.
import { Camera, Input, Key, UI, Vec2 } from "@jm/runtime";

const screen = new Vec2();
const world = new Vec2();
let lastX: f32 = -1, lastY: f32 = -1;
let movedThisFrame = false;

export class Pointer {
  static update(): void {
    Input.pointer(screen);
    Camera.toWorld(screen.x, screen.y, world);
    movedThisFrame = screen.x != lastX || screen.y != lastY;
    lastX = screen.x;
    lastY = screen.y;
  }

  static get inside(): bool { return Input.pointerInside; }
  static get x(): f32 { return world.x; }
  static get y(): f32 { return world.y; }
  // Hover only takes over a selection when the mouse moves, so a resting
  // pointer doesn't fight the arrow keys.
  static get moved(): bool { return movedThisFrame && Input.pointerInside; }

  static get clicked(): bool { return Input.pointerInside && Input.keyPressed(Key.MouseLeft); }
  static get rightClicked(): bool { return Input.pointerInside && Input.keyPressed(Key.MouseRight); }

  // Over a UI element (by id) as it was last drawn; hidden elements are never under it.
  static over(id: string): bool {
    if (!Input.pointerInside) return false;
    const r = UI.worldRect(id);
    return r != null && r.right > r.left && r.contains(world.x, world.y);
  }

  // The first of `count` numbered elements (prefix1..prefixN) under the pointer, or -1.
  static overRow(prefix: string, count: i32): i32 {
    for (let i = 0; i < count; i++) if (Pointer.over(prefix + (i + 1).toString())) return i;
    return -1;
  }
}
