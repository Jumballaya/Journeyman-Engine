import { __jmWorldFindFirst, __jmWorldFindAll, __jmWorldSpawn } from "./env";
import { Entity } from "./entity";
import { utf8 } from "./util";
import { jsonString, jsonNumber } from "./json";

// Changes applied to a prefab's components as it spawns, e.g.
//   new Overrides().velocity(0, -200).param("pattern", "dive")
export class Overrides {
  private components: Map<string, Map<string, string>> = new Map();
  private params: Map<string, string> = new Map();
  private tags: string[] = [];

  velocity(x: f32, y: f32): Overrides { return this.json("VelocityComponent", "velocity", `[${jsonNumber(x)},${jsonNumber(y)}]`); }
  acceleration(x: f32, y: f32): Overrides { return this.json("VelocityComponent", "acceleration", `[${jsonNumber(x)},${jsonNumber(y)}]`); }
  rotation(radians: f32): Overrides { return this.json("TransformComponent", "rotation", jsonNumber(radians)); }
  scale(x: f32, y: f32): Overrides { return this.json("TransformComponent", "scale", `[${jsonNumber(x)},${jsonNumber(y)}]`); }
  tint(r: f32, g: f32, b: f32, a: f32 = 1): Overrides {
    return this.json("SpriteComponent", "color", `[${jsonNumber(r)},${jsonNumber(g)},${jsonNumber(b)},${jsonNumber(a)}]`);
  }
  texture(asset: string): Overrides { return this.text("SpriteComponent", "texture", asset); }
  lifetime(seconds: f32): Overrides { return this.json("LifetimeComponent", "seconds", jsonNumber(seconds)); }
  scrollY(min: f32, max: f32): Overrides {
    assert(max > min, "Overrides.scrollY: positive span required");
    return this.json("ScrollWrapComponent", "minY", jsonNumber(min)).json("ScrollWrapComponent", "maxY", jsonNumber(max));
  }

  // A tag for the spawned entity, so World.find can name it.
  tag(name: string): Overrides { this.tags.push(name); return this; }

  // ScriptComponent params, read by the spawned script with Params.
  param(key: string, value: f64): Overrides { this.params.set(key, jsonNumber(value)); return this; }
  paramText(key: string, value: string): Overrides { this.params.set(key, jsonString(value)); return this; }
  text(component: string, property: string, value: string): Overrides {
    return this.json(component, property, jsonString(value));
  }

  // Escape hatch for custom components. Repeated properties replace their old
  // value. Use param/paramText for ScriptComponent.params, not raw JSON.
  json(component: string, property: string, rawJson: string): Overrides {
    assert(component != "ScriptComponent" || property != "params", "Use param/paramText for script parameters");
    if (!this.components.has(component)) this.components.set(component, new Map<string, string>());
    this.components.get(component).set(property, rawJson);
    return this;
  }

  toJson(): string {
    const parts = new Array<string>();
    const names = this.components.keys();
    if (this.params.size > 0 && !this.components.has("ScriptComponent")) names.push("ScriptComponent");
    for (let i = 0; i < names.length; i++) {
      const props = this.components.has(names[i]) ? encodeProperties(this.components.get(names[i])) : new Array<string>();
      if (names[i] == "ScriptComponent" && this.params.size > 0) props.push(`"params":{${encodeProperties(this.params).join(",")}}`);
      parts.push(`${jsonString(names[i])}:{${props.join(",")}}`);
    }
    if (this.tags.length > 0) parts.push(`"tags":[${this.tags.map<string>((t: string) => jsonString(t)).join(",")}]`);
    return `{${parts.join(",")}}`;
  }
}

function encodeProperties(properties: Map<string, string>): string[] {
  const keys = properties.keys();
  const result = new Array<string>();
  for (let i = 0; i < keys.length; i++) result.push(`${jsonString(keys[i])}:${properties.get(keys[i])}`);
  return result;
}

// Creates a prefab ("bullet" or "assets/prefabs/bullet.prefab.json") at
// (x, y) at the end of this frame and returns its handle right away. Spawned
// entities belong to the current scene.
export function spawn(prefab: string, x: f32, y: f32, overrides: Overrides | null = null): Entity {
  const p = utf8(prefab);
  const o = utf8(overrides === null ? "" : overrides.toJson());
  return Entity.unpack(__jmWorldSpawn(p.dataStart, p.length, x, y, o.dataStart, o.length));
}

// Live entities by tag. Scene entities are tagged with their name.
export class World {
  // Any one entity with the tag, or Entity.NONE.
  static find(tag: string): Entity {
    const t = utf8(tag);
    return Entity.unpack(__jmWorldFindFirst(t.dataStart, t.length));
  }

  static findAll(tag: string): Entity[] {
    const t = utf8(tag);
    let ids = new Uint32Array(128);
    let count = __jmWorldFindAll(t.dataStart, t.length, ids.dataStart, ids.byteLength);
    if (count * 2 > ids.length) {
      ids = new Uint32Array(count * 2);
      count = min(__jmWorldFindAll(t.dataStart, t.length, ids.dataStart, ids.byteLength), count);
    }
    const out = new Array<Entity>(count);
    for (let i = 0; i < count; i++) out[i] = new Entity(ids[i * 2], ids[i * 2 + 1]);
    return out;
  }

  // Destroy every live match, e.g. to clear projectiles at the end of a fight.
  static destroyAll(tag: string): void {
    const entities = World.findAll(tag);
    for (let i = 0; i < entities.length; i++) entities[i].destroy();
  }

  static count(tag: string): i32 {
    const t = utf8(tag);
    return __jmWorldFindAll(t.dataStart, t.length, 0, 0);
  }
}
