import { __jmLog } from "./env";
import { utf8 } from "./util";

export class Logger {
  // Writes to stdout and the engine log ("[script] ...").
  static log(...messages: string[]): void {
    const view = utf8(messages.join(""));
    __jmLog(<i32>view.dataStart, view.length - 1);
  }
}
