// The player: runs with the arrows, jumps with Space when standing.
import { Camera, CameraFollow, GameState, Input, UI, self } from "@jm/runtime";
import { jumpVelocity, scoreText } from "./lib/rules";

const me = self();
const SPEED: f32 = 180;
const cam = new CameraFollow(0, 0).setBounds(-700, -500, 1300, 400);
cam.deadZoneWidth = 40;
cam.smoothing = 8;
GameState.setNumber("score", 0);

export function onUpdate(dt: f32): void {
  const v = me.velocity;
  v.x = Input.axis("left", "right") * SPEED;
  v.y = jumpVelocity(v.onGround, Input.pressed("jump"), v.y, v.supportVelocityY);
  v.dropThrough = Input.down("down");
  cam.follow(me, dt);
  // The driver has no camera in its state: publish it for headless checks.
  GameState.setNumber("camX", Camera.x);
  GameState.setNumber("camY", Camera.y);
  UI.setText("score", scoreText(GameState.getNumber("score", 0)));
}
