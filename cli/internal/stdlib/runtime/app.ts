import { __jmAppQuit, __jmLog } from "./env";
import { utf8 } from "./util";

export class App {
  // Exits after the current frame.
  static quit(): void { __jmAppQuit(); }
}

// Prints to stdout and the engine log: log("hp ", hp.toString()).
export function log(a: string, b: string = "", c: string = "", d: string = ""): void {
  const m = utf8(a + b + c + d);
  __jmLog(m.dataStart, m.length);
}
