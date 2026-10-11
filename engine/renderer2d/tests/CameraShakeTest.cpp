#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "../../core/tests/assets/TempDir.hpp"
#include "Engine.hpp"
#include "Renderer2DModule.hpp"

namespace {

// A script whose onUpdate(f32) calls __jmCameraShake(10, 1). Hand-encoded: no wasm toolchain.
const std::vector<uint8_t> kShakeWasm = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x0a, 0x02, 0x60, 0x02, 0x7d, 0x7d, 0x00, 0x60, 0x01, 0x7d, 0x00,
    0x02, 0x17, 0x01, 0x03, 'e', 'n', 'v', 0x0f, '_', '_', 'j', 'm', 'C', 'a', 'm', 'e', 'r', 'a', 'S', 'h', 'a', 'k',
    'e', 0x00, 0x00,
    0x03, 0x02, 0x01, 0x01,
    0x07, 0x0c, 0x01, 0x08, 'o', 'n', 'U', 'p', 'd', 'a', 't', 'e', 0x00, 0x01,
    0x0a, 0x10, 0x01, 0x0e, 0x00, 0x43, 0x00, 0x00, 0x20, 0x41, 0x43, 0x00, 0x00, 0x80, 0x3f, 0x10, 0x00, 0x0b,
};

}  // namespace

// Shake belongs to the scene that started it: a frame drawn after it's
// unloaded, before the next scene's first step, has no shake.
TEST(CameraShake, AnUnloadedScenesShakeIsntDrawn) {
  TempDir game;
  game.writeFile("shake.ts", kShakeWasm);
  game.writeFile(".jm.json", R"({"name": "S", "entryScene": "shaking.scene.json",
                                 "scenes": ["shaking.scene.json", "still.scene.json"], "assets": ["shake.ts"]})");
  game.writeFile("shaking.scene.json",
                 R"({"name": "shaking", "entities": [{"name": "s", "components": {"ScriptComponent": {"script": "shake.ts"}}}]})");
  game.writeFile("still.scene.json", R"({"name": "still", "entities": []})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.renderer = "none";
  options.dev.saveDir = game.path() / "save";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  Camera2D& camera = engine.getModules().find<Renderer2DModule>()->renderer().camera();
  engine.stepOnce();
  engine.stepOnce();
  ASSERT_NE(camera.position(), glm::vec2(0.0f));  // shaking

  engine.getSceneManager().loadScene("still.scene.json");
  const uint64_t steps = engine.frameCount();
  engine.frame(0.0f);
  EXPECT_EQ(engine.frameCount(), steps);
  EXPECT_EQ(camera.position(), glm::vec2(0.0f));
}
