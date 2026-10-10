import { GameState, Params, Time, World, self } from "@jm/runtime";

// A cable's rope: from its anchor to whoever holds it, or swaying a little.
const me = self();
const index = Params.number("index", 0);
const length = <f32>Params.number("length", 200);
const anchorX = me.transform.x, anchorY = me.transform.y;

export function onUpdate(dt: f32): void {
  let tipX = anchorX + Mathf.sin(<f32>Time.elapsed * 1.3) * 10, tipY = anchorY - length;
  if (GameState.getNumber("holding") == index + 1) {
    const hero = World.find("Kage");
    tipX = hero.transform.x;
    tipY = hero.transform.y;
  }
  const dx = tipX - anchorX, dy = tipY - anchorY;
  const reach = Mathf.sqrt(dx * dx + dy * dy);
  me.transform.setPosition((anchorX + tipX) * 0.5, (anchorY + tipY) * 0.5);
  me.transform.rotation = Mathf.atan2(dy, dx) + Mathf.PI / 2;  // the texture runs up
  me.transform.setScale(6, reach * 0.5);
}
