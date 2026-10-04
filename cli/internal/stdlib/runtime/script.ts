import { __jmScriptParamNumber, __jmScriptParamString } from "./env";
import { utf8, scratchPtr, scratchCap, needsRetry, scratchString } from "./util";

// Per-instance parameters authored on the ScriptComponent:
//   "ScriptComponent": { "script": "...", "params": { "speed": 120, "kind": "zigzag" } }
// Prefab spawns can override them: World.spawn(p, x, y, '{"ScriptComponent":{"params":{...}}}').
export class Params {
  static number(key: string, fallback: f64 = 0): f64 {
    const k = utf8(key);
    return __jmScriptParamNumber(<i32>k.dataStart, k.length - 1, fallback);
  }

  static string(key: string, fallback: string = ""): string {
    const k = utf8(key);
    let n = __jmScriptParamString(<i32>k.dataStart, k.length - 1, scratchPtr(), scratchCap());
    if (needsRetry(n)) n = __jmScriptParamString(<i32>k.dataStart, k.length - 1, scratchPtr(), scratchCap());
    return n < 0 ? fallback : scratchString(n);
  }
}
