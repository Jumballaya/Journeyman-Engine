import { __jmParamNumber, __jmParamString } from "./env";
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
