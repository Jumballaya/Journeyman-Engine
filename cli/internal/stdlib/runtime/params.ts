import { __jmParamNumber, __jmParamString, __jmEntityParamNumber, __jmEntityParamString } from "./env";
import { utf8, buf, cap, grow, text } from "./util";

// This instance's ScriptComponent "params" (set in scenes, prefabs or
// spawn Overrides), so one script can drive many variants.
export class Params {
  static number(key: string, fallback: f64 = 0): f64 {
    const k = utf8(key);
    return __jmParamNumber(k.dataStart, k.length, fallback);
  }

  static text(key: string, fallback: string = ""): string {
    const k = utf8(key);
    let n = __jmParamString(k.dataStart, k.length, buf(), cap());
    if (grow(n)) n = __jmParamString(k.dataStart, k.length, buf(), cap());
    return text(n, fallback);
  }
}

// Another entity's script params (entity.params); fallbacks if it has no script.
export class EntityParams {
  constructor(readonly index: u32, readonly generation: u32) {}

  number(key: string, fallback: f64 = 0): f64 {
    const k = utf8(key);
    return __jmEntityParamNumber(this.index, this.generation, k.dataStart, k.length, fallback);
  }

  text(key: string, fallback: string = ""): string {
    const k = utf8(key);
    let n = __jmEntityParamString(this.index, this.generation, k.dataStart, k.length, buf(), cap());
    if (grow(n)) n = __jmEntityParamString(this.index, this.generation, k.dataStart, k.length, buf(), cap());
    return text(n, fallback);
  }
}
