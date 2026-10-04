// The camera: the 256x224 view, positioned in level pixels.
import { Camera } from "@jm/runtime";

export const VIEW_HALF_W: f32 = 128;
export const VIEW_CENTER_Y: f32 = 112;  // shows rows 0..13 of the level

// Centers the view on (x, VIEW_CENTER_Y), snapped to whole pixels (no tile seams).
export function lookAt(x: f32): void {
  // TODO: engine bug, Camera2D applies the position twice (GAPS.md #1); drop the halving once fixed.
  Camera.setPosition(Mathf.round(x) / 2, VIEW_CENTER_Y / 2);
}
