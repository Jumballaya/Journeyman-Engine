import {
  __jmKeyIsPressed, __jmKeyIsReleased, __jmKeyIsDown,
  __jmActionState, __jmActionValue, __jmActionBind, __jmActionUnbind, __jmGamepadConnected,
} from "./env";
import { utf8 } from "./util";

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
};

export class Inputs {

    public static keyIsPressed(key: Key): boolean {
        return __jmKeyIsPressed(<i32>key) != 0;
    }

    public static keyIsReleased(key: Key): boolean {
        return __jmKeyIsReleased(<i32>key) != 0;
    }

    public static keyIsDown(key: Key): boolean {
        return __jmKeyIsDown(<i32>key) != 0;
    }

};

// Named actions defined in a .bindings.json asset (or bound at runtime), each
// mapped to keys and/or gamepad controls. Prefer these over raw keys so the
// game works with keyboard and controller alike.
export class Input {
  // Held this frame.
  static down(action: string): bool {
    const a = utf8(action);
    return __jmActionState(<i32>a.dataStart, a.length - 1, 0) != 0;
  }

  // Went down this frame.
  static pressed(action: string): bool {
    const a = utf8(action);
    return __jmActionState(<i32>a.dataStart, a.length - 1, 1) != 0;
  }

  // Went up this frame.
  static released(action: string): bool {
    const a = utf8(action);
    return __jmActionState(<i32>a.dataStart, a.length - 1, 2) != 0;
  }

  // 0..1; analog for sticks/triggers, 0 or 1 for keys and buttons.
  static value(action: string): f32 {
    const a = utf8(action);
    return __jmActionValue(<i32>a.dataStart, a.length - 1);
  }

  // value(positive) - value(negative), e.g. Input.axis("left", "right").
  static axis(negative: string, positive: string): f32 {
    return Input.value(positive) - Input.value(negative);
  }

  // Adds a control ("Space", "Gamepad.A", "Gamepad.LeftStickUp") to an action.
  static bind(action: string, control: string): bool {
    const a = utf8(action);
    const c = utf8(control);
    return __jmActionBind(<i32>a.dataStart, a.length - 1, <i32>c.dataStart, c.length - 1) != 0;
  }

  static unbind(action: string): void {
    const a = utf8(action);
    __jmActionUnbind(<i32>a.dataStart, a.length - 1);
  }

  static get gamepadConnected(): bool {
    return __jmGamepadConnected() != 0;
  }
}
