// The camera: one 256x176 room below a 48px HUD, in area pixels.
import { Camera } from "@jm/runtime";
import { ROOM_W, ROOM_H } from "./areas";
import { TILE } from "./tiles";

const HUD_HEIGHT: f32 = 48;

export function roomCenterX(rx: i32): f32 { return (<f32>rx + 0.5) * <f32>ROOM_W * TILE; }
export function roomCenterY(ry: i32): f32 { return (<f32>ry + 0.5) * <f32>ROOM_H * TILE; }

// Shows the room centered at (x, y), leaving the top of the screen for the HUD.
export function lookAt(x: f32, y: f32): void {
  // TODO: engine bug, Camera2D applies the position twice (GAPS.md #1); drop the halving once fixed.
  Camera.setPosition(Mathf.round(x) / 2, Mathf.round(y + HUD_HEIGHT / 2) / 2);
}
