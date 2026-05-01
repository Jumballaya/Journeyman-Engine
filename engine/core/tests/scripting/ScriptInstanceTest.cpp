#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "../../assets/AssetManager.hpp"
#include "../../scripting/HostFunction.hpp"
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

// Counter for the host_a callback. Reset at each test entry. Not threadsafe;
// tests run serially on a single ScriptInstance so a plain int suffices.
int g_hostACallCount = 0;
int g_hostBCallCount = 0;

const void* hostA(IM3Runtime, IM3ImportContext, uint64_t*, void*) {
  ++g_hostACallCount;
  return nullptr;
}

const void* hostB(IM3Runtime, IM3ImportContext, uint64_t*, void*) {
  ++g_hostBCallCount;
  return nullptr;
}

}  // namespace

// Register a host function the wasm doesn't import. Instance constructs
// without throwing — the link-all loop swallows m3Err_functionLookupFailed.
TEST(ScriptInstance, SilentlyIgnoresHostFunctionsModuleDoesNotImport) {
  ScriptManager sm;
  sm.registerHostFunction("host_a", {"env", "host_a", "v()", &hostA});

  std::vector<uint8_t> wasm(std::begin(kMinimalUpdateWasm),
                            std::end(kMinimalUpdateWasm));
  AssetHandle handle{1};
  ASSERT_NO_THROW(sm.loadScript(handle, wasm));

  g_hostACallCount = 0;
  ScriptInstanceHandle inst = sm.createInstance(handle, EntityId{0, 0});
  EXPECT_TRUE(inst.isValid());
  EXPECT_EQ(g_hostACallCount, 0);
}

// Register multiple host functions; module imports one. The linker attempts
// every host function, succeeds for the imported one, swallows lookup-failed
// for the others. Calling onUpdate dispatches into the linked host_a, proving
// the link landed.
TEST(ScriptInstance, LinksAllRegisteredHostFunctionsThatModuleImports) {
  ScriptManager sm;
  sm.registerHostFunction("host_a", {"env", "host_a", "v()", &hostA});
  sm.registerHostFunction("host_b", {"env", "host_b", "v()", &hostB});

  std::vector<uint8_t> wasm(std::begin(kHostImportingWasm),
                            std::end(kHostImportingWasm));
  AssetHandle handle{2};
  ASSERT_NO_THROW(sm.loadScript(handle, wasm));

  g_hostACallCount = 0;
  g_hostBCallCount = 0;

  ScriptInstanceHandle inst = sm.createInstance(handle, EntityId{0, 0});
  ASSERT_TRUE(inst.isValid());

  sm.updateInstance(inst, 0.016f);

  EXPECT_EQ(g_hostACallCount, 1);  // imported + linked + called by onUpdate
  EXPECT_EQ(g_hostBCallCount, 0);  // not imported; linker swallowed the miss
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
