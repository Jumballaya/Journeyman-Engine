// Angles are degrees, counterclockwise from right; rotating pods sweep six degrees each way.
export function podAim(angle: f32, rotates: bool, seconds: f32): f32 {
  return angle + (rotates ? Mathf.sin(seconds * 0.5) * 6 : 0);
}

export class Launch {
  x: f32;
  y: f32;

  constructor(angle: f32, speed: f32) {
    const radians = angle * Mathf.PI / 180;
    this.x = Mathf.cos(radians) * Mathf.max(0, speed);
    this.y = Mathf.sin(radians) * Mathf.max(0, speed);
  }
}
