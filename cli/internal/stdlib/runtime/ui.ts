import { __jmUISetText, __jmUISetClass, __jmUISetStyle, __jmUISetAttribute, __jmUIExists, __jmUIRect } from "./env";
import { utf8 } from "./util";
import { Rect, Vec2, clamp } from "./math";
import { Camera } from "./render";

// Changes the HTML screens on display (UIDocumentComponent), addressing
// elements by `id` across all of them. Calls return false if nothing matched.
export class UI {
  // Replaces the element's content with text.
  static setText(id: string, text: string): bool {
    const i = utf8(id);
    const t = utf8(text);
    return __jmUISetText(i.dataStart, i.length, t.dataStart, t.length);
  }

  static addClass(id: string, cls: string): bool { return UI.toggleClass(id, cls, true); }
  static removeClass(id: string, cls: string): bool { return UI.toggleClass(id, cls, false); }
  static toggleClass(id: string, cls: string, on: bool): bool {
    const i = utf8(id);
    const c = utf8(cls);
    return __jmUISetClass(i.dataStart, i.length, c.dataStart, c.length, on);
  }

  // One inline style property, e.g. setStyle("bar", "width", "40%"); "" removes it.
  static setStyle(id: string, property: string, value: string): bool {
    const i = utf8(id);
    const p = utf8(property);
    const v = utf8(value);
    return __jmUISetStyle(i.dataStart, i.length, p.dataStart, p.length, v.dataStart, v.length);
  }

  static setAttribute(id: string, name: string, value: string): bool {
    const i = utf8(id);
    const n = utf8(name);
    const v = utf8(value);
    return __jmUISetAttribute(i.dataStart, i.length, n.dataStart, n.length, v.dataStart, v.length);
  }

  static show(id: string): bool { return UI.setStyle(id, "display", ""); }
  static hide(id: string): bool { return UI.setStyle(id, "display", "none"); }
  // With a hiddenClass, toggle the document's CSS convention (including
  // !important rules). Otherwise use inline display styles.
  static setVisible(id: string, visible: bool, hiddenClass: string = ""): bool {
    return hiddenClass.length > 0 ? UI.toggleClass(id, hiddenClass, !visible) : visible ? UI.show(id) : UI.hide(id);
  }

  static opacity(id: string, amount: f32): bool { return UI.setStyle(id, "opacity", clamp(amount, 0, 1).toString()); }
  static fill(id: string, fraction: f32): bool {
    return UI.setStyle(id, "width", (clamp(fraction, 0, 1) * 100).toString() + "%");
  }
  // IDs prefix1 .. prefixN, useful for lives, hearts and ammunition.
  static showCount(prefix: string, count: i32, total: i32, hiddenClass: string = ""): void {
    for (let i = 1; i <= total; i++) UI.setVisible(prefix + i.toString(), i <= count, hiddenClass);
  }

  // Where an element is in the world, so sprites can line up with the layout
  // (as of the last frame drawn); null if no element has the id.
  static worldRect(id: string): Rect | null {
    const i = utf8(id);
    const box = new StaticArray<f32>(4);  // x, y, w, h in screen pixels
    if (!__jmUIRect(i.dataStart, i.length, changetype<usize>(box), 16)) return null;
    const topLeft = Camera.toWorld(box[0], box[1], new Vec2());
    const bottomRight = Camera.toWorld(box[0] + box[2], box[1] + box[3], new Vec2());
    return new Rect(topLeft.x, bottomRight.y, bottomRight.x, topLeft.y);
  }

  static exists(id: string): bool {
    const i = utf8(id);
    return __jmUIExists(i.dataStart, i.length);
  }
}
