// @TODO: getDeltaTime is currently a placeholder — `__internal.dt` is never
// updated by the host and therefore always reads 0. Use the `dt` parameter
// passed to `onUpdate(dt: f32)` for real per-frame timing until either a host
// function is added to write `__internal.dt` or this helper is removed.
namespace __internal {
  let dt: f32 = 0;
}

export function getDeltaTime(): f32 {
  return __internal.dt;
}
