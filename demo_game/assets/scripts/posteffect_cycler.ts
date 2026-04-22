// Press P to cycle through the engine's built-in post-effects:
//   None -> Grayscale -> Blur -> Pixelate -> ColorShift -> None

let currentEffect: PostEffect | null = null;
let currentIndex: i32 = 0;

const cycleOrder: Array<BuiltinEffect> = [
  BuiltinEffect.Grayscale,
  BuiltinEffect.Blur,
  BuiltinEffect.Pixelate,
  BuiltinEffect.ColorShift,
];

export function onUpdate(dt: f32): void {
  if (!Inputs.keyIsPressed(Key.P)) {
    return;
  }

  if (currentEffect !== null) {
    (currentEffect as PostEffect).remove();
    currentEffect = null;
  }

  if (currentIndex < cycleOrder.length) {
    const effect = new PostEffect(cycleOrder[currentIndex]);
    currentEffect = effect;
    console.log("[posteffect_cycler] enabled effect ", currentIndex.toString());
  } else {
    console.log("[posteffect_cycler] cleared effects");
  }

  currentIndex = (currentIndex + 1) % (cycleOrder.length + 1);
}
