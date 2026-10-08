#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../assets/AssetManager.hpp"
#include "../../scripting/LoadedScript.hpp"
#include "../../scripting/ScriptInstance.hpp"
#include "../../scripting/ScriptInstanceHandle.hpp"
#include "../../scripting/ScriptManager.hpp"
#include "../assets/TempDir.hpp"

namespace {

// A minimal valid wasm module that exports `onUpdate(f32) -> void` with an
// empty body and no imports. Mirrors the fixture in SceneManagerTest.cpp.
constexpr uint8_t kMinimalUpdateWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x01, 0x7d, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 0x6f, 0x6e, 0x55, 0x70, 0x64, 0x61, 0x74, 0x65,
    0x00, 0x00,
    0x0a, 0x04, 0x01, 0x02, 0x00, 0x0b,
};

// A wasm module that imports `env.host_a` (() -> ()) and exports
// `onUpdate(f32) -> void`. The body of onUpdate calls host_a so we can verify
// the import was actually linked.
//
// Sections:
//   magic+version       0x00 0x61 0x73 0x6d 0x01 0x00 0x00 0x00
//   type:    type 0 = ()->(), type 1 = (f32)->()
//                       0x01 0x08 0x02 0x60 0x00 0x00 0x60 0x01 0x7d 0x00
//   import:  env.host_a, type 0  (function index 0)
//                       0x02 0x0e 0x01 0x03 'e' 'n' 'v' 0x06 'h' 'o' 's' 't'
//                       '_' 'a' 0x00 0x00
//   func:    one local function of type 1  (function index 1)
//                       0x03 0x02 0x01 0x01
//   export:  "onUpdate" -> function index 1
//                       0x07 0x0c 0x01 0x08 'o' 'n' 'U' 'p' 'd' 'a' 't' 'e'
//                       0x00 0x01
//   code:    body { call 0; end }
//                       0x0a 0x06 0x01 0x04 0x00 0x10 0x00 0x0b
constexpr uint8_t kHostImportingWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x08, 0x02, 0x60, 0x00, 0x00, 0x60, 0x01, 0x7d, 0x00,
    0x02, 0x0e, 0x01,
    0x03, 0x65, 0x6e, 0x76,
    0x06, 0x68, 0x6f, 0x73, 0x74, 0x5f, 0x61,
    0x00, 0x00,
    0x03, 0x02, 0x01, 0x01,
    0x07, 0x0c, 0x01, 0x08, 0x6f, 0x6e, 0x55, 0x70, 0x64, 0x61, 0x74, 0x65,
    0x00, 0x01,
    0x0a, 0x06, 0x01, 0x04, 0x00, 0x10, 0x00, 0x0b,
};

// Wasm importing env.echo(ptr, len) -> () with "hi" at address 8 and a
// 1-page memory; onUpdate calls echo(<ptr>, 2). <ptr> is a 3-byte LEB.
std::vector<uint8_t> echoWasm(uint8_t p0, uint8_t p1, uint8_t p2) {
  return {
      0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
      0x01, 0x0a, 0x02, 0x60, 0x02, 0x7f, 0x7f, 0x00, 0x60, 0x01, 0x7d, 0x00,  // types
      0x02, 0x0c, 0x01, 0x03, 'e', 'n', 'v', 0x04, 'e', 'c', 'h', 'o', 0x00, 0x00,  // import
      0x03, 0x02, 0x01, 0x01,  // func 1: type 1
      0x05, 0x03, 0x01, 0x00, 0x01,  // memory: 1 page
      0x07, 0x0c, 0x01, 0x08, 'o', 'n', 'U', 'p', 'd', 'a', 't', 'e', 0x00, 0x01,
      0x0a, 0x0c, 0x01, 0x0a, 0x00, 0x41, p0, p1, p2, 0x41, 0x02, 0x10, 0x00, 0x0b,  // code
      0x0b, 0x08, 0x01, 0x00, 0x41, 0x08, 0x0b, 0x02, 'h', 'i',  // data
  };
}

ScriptInstance* start(ScriptManager& sm, const std::vector<uint8_t>& wasm, AssetHandle handle) {
  sm.loadScript(handle, wasm);
  return sm.getInstance(sm.createInstance(handle, EntityId{0, 0}));
}

}  // namespace

// Register a host function the wasm doesn't import. Instance constructs
// without throwing — the link-all loop swallows m3Err_functionLookupFailed.
TEST(ScriptInstance, SilentlyIgnoresHostFunctionsModuleDoesNotImport) {
  ScriptManager sm;
  int calls = 0;
  sm.bind("host_a", [&] { ++calls; });

  std::vector<uint8_t> wasm(std::begin(kMinimalUpdateWasm),
                            std::end(kMinimalUpdateWasm));
  AssetHandle handle{1};
  ASSERT_NO_THROW(sm.loadScript(handle, wasm));

  ScriptInstanceHandle inst = sm.createInstance(handle, EntityId{0, 0});
  EXPECT_TRUE(inst.isValid());
  EXPECT_EQ(calls, 0);
}

// Register multiple host functions; module imports one. The linker attempts
// every host function, succeeds for the imported one, swallows lookup-failed
// for the others. Calling onUpdate dispatches into the linked host_a, proving
// the link landed.
TEST(ScriptInstance, LinksAllRegisteredHostFunctionsThatModuleImports) {
  ScriptManager sm;
  int aCalls = 0, bCalls = 0;
  sm.bind("host_a", [&] { ++aCalls; });
  sm.bind("host_b", [&] { ++bCalls; });

  std::vector<uint8_t> wasm(std::begin(kHostImportingWasm),
                            std::end(kHostImportingWasm));
  ScriptInstance* inst = start(sm, wasm, AssetHandle{2});
  ASSERT_NE(inst, nullptr);
  inst->update(0.016f);

  EXPECT_EQ(aCalls, 1);  // imported, linked, called by onUpdate
  EXPECT_EQ(bCalls, 0);  // not imported
}

// The .ts extension converter (Engine.cpp registers it during initialize)
// treats the asset bytes as wasm. Folder-mode iteration: dropping a `.ts`
// file in the mounted root and calling loadAsset routes through to
// ScriptManager.loadScript and caches a LoadedScript.
TEST(ScriptInstance, TsExtensionConverterRegistersAndDispatches) {
  TempDir dir;
  std::vector<uint8_t> wasm(std::begin(kMinimalUpdateWasm),
                            std::end(kMinimalUpdateWasm));
  dir.writeFile("foo.ts", wasm);

  AssetManager assets(dir.path());
  ScriptManager sm;

  // Mirror Engine::registerScriptModule's `.ts` registration in isolation —
  // this test pins the converter shape without bringing up the full Engine.
  assets.addAssetConverter({".ts"},
      [&sm](const RawAsset& asset, const AssetHandle& handle) {
        sm.loadScript(handle, asset.data);
      });

  AssetHandle handle;
  ASSERT_NO_THROW(handle = assets.loadAsset("foo.ts"));
  ASSERT_TRUE(handle.isValid());

  const LoadedScript* loaded = sm.getScript(handle);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->binary, wasm);
}

// std::string parameters arrive as (ptr, len) and are copied out of script memory.
TEST(HostBinding, DecodesStringArguments) {
  ScriptManager sm;
  std::string seen;
  sm.bind("echo", [&](std::string text) { seen = text; });

  ScriptInstance* inst = start(sm, echoWasm(0x88, 0x80, 0x00), AssetHandle{3});  // ptr 8
  ASSERT_NE(inst, nullptr);
  inst->update(0.016f);
  EXPECT_EQ(seen, "hi");
  EXPECT_FALSE(inst->failed());
}

// A pointer outside script memory traps the script instead of reading garbage.
TEST(HostBinding, OutOfBoundsPointerDisablesScript) {
  ScriptManager sm;
  int calls = 0;
  sm.bind("echo", [&](std::string) { ++calls; });

  ScriptInstance* inst = start(sm, echoWasm(0xff, 0xff, 0x3f), AssetHandle{4});  // ptr 1 MiB
  ASSERT_NE(inst, nullptr);
  inst->update(0.016f);
  inst->update(0.016f);
  EXPECT_EQ(calls, 0);
  EXPECT_TRUE(inst->failed());
}

// Exceptions never unwind through wasm: the script is trapped and disabled.
TEST(HostBinding, ThrowingHostFunctionDisablesScript) {
  ScriptManager sm;
  int calls = 0;
  sm.bind("echo", [&](std::string) {
    ++calls;
    throw std::runtime_error("nope");
  });

  ScriptInstance* inst = start(sm, echoWasm(0x88, 0x80, 0x00), AssetHandle{5});
  ASSERT_NE(inst, nullptr);
  inst->update(0.016f);
  inst->update(0.016f);
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(inst->failed());
}

// Signatures are derived from the callable's C++ types.
TEST(HostBinding, DerivesWasmSignatures) {
  using host::BoundFunction;
  auto sig = [](auto fn) { return BoundFunction<decltype(fn)>(fn).signature(); };
  EXPECT_EQ(sig([] {}), "v()");
  EXPECT_EQ(sig([](float, double, int32_t, bool) { return 1; }), "i(fFii)");
  EXPECT_EQ(sig([](std::string, EntityId) { return EntityId{}; }), "I(iiii)");
  EXPECT_EQ(sig([](host::ScriptCall&, host::WasmBytes) { return 0.0f; }), "f(ii)");
  EXPECT_EQ(sig([](std::string) -> std::optional<std::string> { return {}; }), "i(iiii)");
}

namespace {

// onUpdate(f32) with `body` (locals, instructions and the final end).
std::vector<uint8_t> updateWith(std::vector<uint8_t> body) {
  std::vector<uint8_t> wasm = {
      0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
      0x01, 0x05, 0x01, 0x60, 0x01, 0x7d, 0x00,  // type 0: (f32) -> ()
      0x03, 0x02, 0x01, 0x00,                    // func 0: type 0
      0x07, 0x0c, 0x01, 0x08, 'o', 'n', 'U', 'p', 'd', 'a', 't', 'e', 0x00, 0x00,
  };
  const auto bodySize = static_cast<uint8_t>(body.size());
  wasm.insert(wasm.end(), {0x0a, static_cast<uint8_t>(bodySize + 2), 0x01, bodySize});
  wasm.insert(wasm.end(), body.begin(), body.end());
  return wasm;
}

// loop { br 0 }: spins forever, calling nothing.
const std::vector<uint8_t> kEndlessLoop = {0x00, 0x03, 0x40, 0x0c, 0x00, 0x0b, 0x0b};

// i = 0; do { i += 1 } while (i < 1'000'000): a million steps, then done.
const std::vector<uint8_t> kMillionSteps = {
    0x01, 0x01, 0x7f,                    // one i32 local
    0x03, 0x40,                          // loop
    0x20, 0x00, 0x41, 0x01, 0x6a,        //   i + 1
    0x22, 0x00,                          //   i = that
    0x41, 0xc0, 0x84, 0x3d, 0x48,        //   < 1'000'000
    0x0d, 0x00,                          //   br_if 0
    0x0b, 0x0b,                          // end loop, end function
};

}  // namespace

// A script stuck in a loop is stopped and disabled; the game goes on.
TEST(ScriptInstance, AnEndlessLoopRunsOutOfFuel) {
  ScriptManager sm;
  ScriptInstance* script = start(sm, updateWith(kEndlessLoop), AssetHandle{1});
  ASSERT_NE(script, nullptr);
  const auto begin = std::chrono::steady_clock::now();
  script->update(0.016f);
  EXPECT_TRUE(script->failed());
  EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::seconds(10));
  script->update(0.016f);  // disabled: returns at once
}

TEST(ScriptInstance, BusyButFiniteWorkIsFine) {
  ScriptManager sm;
  ScriptInstance* script = start(sm, updateWith(kMillionSteps), AssetHandle{1});
  ASSERT_NE(script, nullptr);
  script->update(0.016f);
  EXPECT_FALSE(script->failed());
}

// Fuel is per call: 30 calls of a million steps each is more than one call's
// budget in all, and fine.
TEST(ScriptInstance, FuelRefillsForEveryCall) {
  ScriptManager sm;
  ScriptInstance* script = start(sm, updateWith(kMillionSteps), AssetHandle{1});
  ASSERT_NE(script, nullptr);
  for (int frame = 0; frame < 30; ++frame) script->update(0.016f);
  EXPECT_FALSE(script->failed());
}
