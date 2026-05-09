import { __jmSpriteSetAnimation } from "./env";

export class Sprite {
  // Switch the currently-playing animation on a given entity. The animation
  // name must match a key in the entity's SpriteAnimationComponent.animations
  // map — unknown names are logged and ignored on the host side. Pass both
  // halves of EntityId; scripts already receive both halves from collision
  // callbacks.
  public static setAnimation(entityIndex: u32, entityGeneration: u32, name: string): void {
    const utf8 = String.UTF8.encode(name, true);
    const view = Uint8Array.wrap(utf8);
    __jmSpriteSetAnimation(<i32>entityIndex, <i32>entityGeneration, <i32>view.dataStart, view.length - 1);
  }
};
