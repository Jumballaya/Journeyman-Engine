import {
  __jmKeyState, __jmActionState, __jmActionValue, __jmActionRepeated, __jmActionBind, __jmActionUnbind,
  __jmGamepadConnected,
} from "./env";
import { utf8 } from "./util";
import { Vec2 } from "./math";

const DOWN = 0;
const PRESSED = 1;
const RELEASED = 2;

// Named actions ("fire", "left") from a .bindings.json asset, each mapped to
// keys and gamepad controls, so games work on keyboard and controller alike.
// Raw keys are available too: Input.keyPressed(Key.F11).
export class Input {
  static down(action: string): bool { return actionState(action, DOWN); }
  static pressed(action: string): bool { return actionState(action, PRESSED); }    // this frame
  static released(action: string): bool { return actionState(action, RELEASED); }  // this frame

  // True when pressed, then every `interval` seconds once held for `delay`:
  // menu cursors, grid movement, falling-block shifts.
  static repeated(action: string, delay: f32 = 0.25, interval: f32 = 0.05): bool {
    const a = utf8(action);
    return __jmActionRepeated(a.dataStart, a.length, delay, interval);
  }

  // 0..1: analog for sticks and triggers, 0 or 1 for keys and buttons.
  static value(action: string): f32 {
    const a = utf8(action);
    return __jmActionValue(a.dataStart, a.length);
  }

  // value(positive) - value(negative), e.g. Input.axis("left", "right").
  static axis(negative: string, positive: string): f32 {
    return Input.value(positive) - Input.value(negative);
  }

  // Four actions as a vector capped at unit length. Partial analog input stays
  // partial; a caller-owned output avoids allocating on every frame.
  static vector(left: string, right: string, down: string, up: string, out: Vec2): Vec2 {
    return out.set(Input.axis(left, right), Input.axis(down, up)).limit();
  }

  // Adds a control ("Space", "Shift" for either Shift key, "Gamepad.A",
  // "Gamepad.LeftStickUp"); false if unknown.
  static bind(action: string, control: string): bool {
    const a = utf8(action);
    const c = utf8(control);
    return __jmActionBind(a.dataStart, a.length, c.dataStart, c.length);
  }

  static unbind(action: string): void {
    const a = utf8(action);
    __jmActionUnbind(a.dataStart, a.length);
  }

  static get gamepadConnected(): bool { return __jmGamepadConnected(); }

  static keyDown(key: Key): bool { return __jmKeyState(<i32>key, DOWN); }
  static keyPressed(key: Key): bool { return __jmKeyState(<i32>key, PRESSED); }
  static keyReleased(key: Key): bool { return __jmKeyState(<i32>key, RELEASED); }
}

function actionState(action: string, query: i32): bool {
  const a = utf8(action);
  return __jmActionState(a.dataStart, a.length, query);
}

// Physical key positions (US layout names).
export enum Key {
  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,
  Digit0,
  Digit1,
  Digit2,
  Digit3,
  Digit4,
  Digit5,
  Digit6,
  Digit7,
  Digit8,
  Digit9,
  Minus,
  Equal,
  Backtick,
  LeftBracket,
  RightBracket,
  Backslash,
  Semicolon,
  Apostrophe,
  Comma,
  Period,
  Slash,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  F13,
  F14,
  F15,
  F16,
  F17,
  F18,
  F19,
  F20,
  F21,
  F22,
  F23,
  F24,
  Escape,
  Tab,
  Enter,
  Space,
  Backspace,
  Insert,
  Delete,
  Home,
  End,
  PageUp,
  PageDown,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  CapsLock,
  NumLock,
  ScrollLock,
  PrintScreen,
  Pause,
  KP0,
  KP1,
  KP2,
  KP3,
  KP4,
  KP5,
  KP6,
  KP7,
  KP8,
  KP9,
  KPPeriod,
  KPEnter,
  KPAdd,
  KPSubtract,
  KPMultiply,
  KPDivide,
  LeftShift,
  RightShift,
  LeftCtrl,
  RightCtrl,
  LeftAlt,
  RightAlt,
  LeftSuper,
  RightSuper,
}
