// The camera: a 320x240 view following a point, kept inside the map.
import { Camera } from "@jm/runtime";

const HALF_W: f32 = 160;
const HALF_H: f32 = 120;

// Centers the view on (x, y) within a map of the given pixel size.
export function follow(x: f32, y: f32, mapW: f32, mapH: f32): void {
  const cx = Mathf.max(HALF_W, Mathf.min(mapW - HALF_W, x));
  const cy = Mathf.max(HALF_H, Mathf.min(mapH - HALF_H, y));
  center(cx, cy);
}

export function center(x: f32, y: f32): void {
  Camera.setPosition(Mathf.round(x), Mathf.round(y));
}
