import { CartPhase, CartRun, CRUISE } from "../assets/scripts/lib/cart";
import { Path, Vec2 } from "@jm/runtime";

export function waitsForBoardingAndKeepsGoingDuringJumps(): void {
  const cart = new CartRun();
  cart.tick(1, 2400, false);
  assert(cart.along == 0 && cart.speed == 0 && cart.phase == CartPhase.Waiting);
  cart.tick(0.1, 2400, true);
  assert(cart.speed > 0 && cart.speed < CRUISE);
  const before = cart.along;
  for (let i = 0; i < 120; i++) cart.tick(1.0 / 60, 2400, false);
  assert(cart.along > before && cart.speed == CRUISE);
}

export function brakesMonotonicallyAndParksExactly(): void {
  const cart = new CartRun();
  let braking = false;
  let previous: f32 = 0;
  for (let i = 0; i < 1000; i++) {
    const distance = cart.along;
    cart.tick(1.0 / 60, 2400, true);
    assert(cart.along >= distance && cart.along <= 2400);
    if (braking) assert(cart.speed <= previous);
    braking = cart.phase == CartPhase.Braking || cart.phase == CartPhase.Parked;
    previous = cart.speed;
  }
  assert(cart.phase == CartPhase.Parked && cart.along == 2400 && cart.speed == 0);
  cart.tick(1, 2400, true);
  assert(cart.along == 2400 && cart.speed == 0);
}

export function resetsAfterFailureAndHandlesShortRails(): void {
  const cart = new CartRun();
  for (let i = 0; i < 100; i++) cart.tick(0.02, 25, true);
  assert(cart.phase == CartPhase.Parked && cart.along == 25);
  cart.reset();
  cart.tick(1, 25, false);
  assert(cart.phase == CartPhase.Waiting && cart.along == 0 && cart.speed == 0);
  cart.tick(0, 25, true);
  assert(cart.along == 0);
}

export function pathSamplesRiseDipAndClampsAtTerminal(): void {
  const rail = new Path([0, 0, 30, 40, 60, 0]);
  const at = new Vec2();
  rail.at(50, at);
  assert(rail.length == 100 && at.x == 30 && at.y == 40);
  rail.direction(75, at);
  assert(at.x > 0 && at.y < 0);
  rail.at(110, at);
  assert(at.x == 60 && at.y == 0);
}
