import { __jmAppQuit } from "./env";

export class App {
  // Ends the game loop after the current frame.
  static quit(): void { __jmAppQuit(); }
}
