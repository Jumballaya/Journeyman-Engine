import { __jmUISetText, __jmUISetClass, __jmUISetStyle, __jmUISetAttribute, __jmUIExists } from "./env";
import { utf8 } from "./util";

// Drives the HTML UI documents currently on screen (entities with a
// UIDocumentComponent). Elements are addressed by their `id` attribute across
// all live documents. Every call returns false when no element matched.
export class UI {
  // Replaces the element's children with a single text node.
  static setText(id: string, text: string): bool {
    const i = utf8(id);
    const t = utf8(text);
    return __jmUISetText(<i32>i.dataStart, i.length - 1, <i32>t.dataStart, t.length - 1) != 0;
  }

  static addClass(id: string, cls: string): bool { return UI.toggleClass(id, cls, true); }
  static removeClass(id: string, cls: string): bool { return UI.toggleClass(id, cls, false); }

  static toggleClass(id: string, cls: string, on: bool): bool {
    const i = utf8(id);
    const c = utf8(cls);
    return __jmUISetClass(<i32>i.dataStart, i.length - 1, <i32>c.dataStart, c.length - 1, on ? 1 : 0) != 0;
  }

  // Sets one inline style property ("opacity", "0.5"); "" removes it.
  static setStyle(id: string, property: string, value: string): bool {
    const i = utf8(id);
    const p = utf8(property);
    const v = utf8(value);
    return __jmUISetStyle(<i32>i.dataStart, i.length - 1, <i32>p.dataStart, p.length - 1,
                          <i32>v.dataStart, v.length - 1) != 0;
  }

  static setAttribute(id: string, name: string, value: string): bool {
    const i = utf8(id);
    const n = utf8(name);
    const v = utf8(value);
    return __jmUISetAttribute(<i32>i.dataStart, i.length - 1, <i32>n.dataStart, n.length - 1,
                              <i32>v.dataStart, v.length - 1) != 0;
  }

  static show(id: string): bool { return UI.setStyle(id, "display", ""); }
  static hide(id: string): bool { return UI.setStyle(id, "display", "none"); }
  static setVisible(id: string, visible: bool): bool { return visible ? UI.show(id) : UI.hide(id); }

  static exists(id: string): bool {
    const i = utf8(id);
    return __jmUIExists(<i32>i.dataStart, i.length - 1) != 0;
  }
}
