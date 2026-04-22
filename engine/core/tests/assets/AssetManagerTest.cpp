#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "AssetManager.hpp"
#include "TempDir.hpp"

// Loading an existing file returns a valid handle whose RawAsset contains the
// file's bytes and the path it was loaded from.
TEST(AssetManager, LoadAssetAndGetRawAssetRoundTrip) {
  TempDir dir;
  const std::string contents = "hello world";
  dir.writeFile("hello.txt", contents);

  AssetManager mgr(dir.path());
  auto handle = mgr.loadAsset("hello.txt");
  ASSERT_TRUE(handle.isValid());

  const RawAsset& asset = mgr.getRawAsset(handle);
  std::string got(asset.data.begin(), asset.data.end());
  EXPECT_EQ(got, contents);
  EXPECT_EQ(asset.filePath.filename().string(), "hello.txt");
}

// getRawAsset with a handle that was never issued by this manager throws.
TEST(AssetManager, GetRawAssetWithInvalidHandleThrows) {
  TempDir dir;
  AssetManager mgr(dir.path());
  AssetHandle bogus{99999};
  EXPECT_THROW(mgr.getRawAsset(bogus), std::runtime_error);
}

// loadAsset propagates the FileSystem error for a missing file rather than
// returning an empty asset silently.
TEST(AssetManager, LoadAssetThrowsOnMissingFile) {
  TempDir dir;
  AssetManager mgr(dir.path());
  EXPECT_THROW(mgr.loadAsset("no-such-file.txt"), std::runtime_error);
}

// Absolute paths are rejected at the AssetManager boundary — paths must be
// manifest-root-relative so the engine behaves identically whether loading
// from loose files or a packed archive.
TEST(AssetManager, LoadAssetRejectsAbsolutePath) {
  TempDir dir;
  dir.writeFile("hello.txt", "bytes");
  AssetManager mgr(dir.path());

  // Build an absolute path that actually exists — the rejection should be
  // path-shape-based, not file-existence-based.
  auto absPath = dir.path() / "hello.txt";
  ASSERT_TRUE(absPath.is_absolute());
  EXPECT_THROW(mgr.loadAsset(absPath), std::runtime_error);

  // Relative path to the same file still works.
  EXPECT_NO_THROW(mgr.loadAsset("hello.txt"));
}

// Loading the same path twice returns the same handle — the manager dedupes
// by path so AssetHandle is a stable shared key across subsystems.
TEST(AssetManager, LoadingSameFileTwiceReturnsSameHandle) {
  TempDir dir;
  dir.writeFile("data.txt", "x");
  AssetManager mgr(dir.path());

  auto h1 = mgr.loadAsset("data.txt");
  auto h2 = mgr.loadAsset("data.txt");
  EXPECT_EQ(h1, h2);
}

// Dedup means converters fire exactly once per unique path — a cached load
// doesn't re-run the conversion.
TEST(AssetManager, RepeatedLoadFiresConverterOnce) {
  TempDir dir;
  dir.writeFile("a.txt", "contents");
  AssetManager mgr(dir.path());

  int calls = 0;
  mgr.addAssetConverter({".txt"}, [&](const RawAsset&, const AssetHandle&) { ++calls; });

  mgr.loadAsset("a.txt");
  mgr.loadAsset("a.txt");
  mgr.loadAsset("a.txt");
  EXPECT_EQ(calls, 1);
}

// A converter registered for a file extension fires on load and receives the
// matching RawAsset and AssetHandle.
TEST(AssetManager, ConverterRunsForMatchingExtension) {
  TempDir dir;
  dir.writeFile("a.txt", "contents");
  AssetManager mgr(dir.path());

  int calls = 0;
  RawAsset capturedAsset;
  AssetHandle capturedHandle{};
  mgr.addAssetConverter({".txt"}, [&](const RawAsset& asset, const AssetHandle& handle) {
    ++calls;
    capturedAsset = asset;
    capturedHandle = handle;
  });

  auto handle = mgr.loadAsset("a.txt");

  EXPECT_EQ(calls, 1);
  EXPECT_EQ(capturedHandle, handle);
  std::string got(capturedAsset.data.begin(), capturedAsset.data.end());
  EXPECT_EQ(got, "contents");
}

// A converter registered for extension .foo is not invoked when a file with
// a different extension is loaded.
TEST(AssetManager, ConverterDoesNotRunForNonMatchingExtension) {
  TempDir dir;
  dir.writeFile("a.bar", "nope");
  AssetManager mgr(dir.path());

  int calls = 0;
  mgr.addAssetConverter({".foo"}, [&](const RawAsset&, const AssetHandle&) { ++calls; });

  mgr.loadAsset("a.bar");
  EXPECT_EQ(calls, 0);
}

// Loading a file whose extension has no registered converter completes
// silently — no error, no callback.
TEST(AssetManager, NoConverterForExtensionIsSilentNoOp) {
  TempDir dir;
  dir.writeFile("unknown.xyz", "data");
  AssetManager mgr(dir.path());

  AssetHandle handle{};
  EXPECT_NO_THROW(handle = mgr.loadAsset("unknown.xyz"));
  EXPECT_TRUE(handle.isValid());
}

// Compound extensions like ".script.json" fire their converters even though
// std::filesystem::path::extension() only returns the last-dot suffix. We
// match every suffix of the filename, longest first. Regressed once when the
// script-loading pipeline went through the asset converter path.
TEST(AssetManager, CompoundExtensionConverterFires) {
  TempDir dir;
  dir.writeFile("player.script.json", "{}");
  AssetManager mgr(dir.path());

  int calls = 0;
  mgr.addAssetConverter({".script.json"},
                        [&](const RawAsset&, const AssetHandle&) { ++calls; });

  mgr.loadAsset("player.script.json");
  EXPECT_EQ(calls, 1);
}

// When a file matches multiple registered suffixes (e.g., ".script.json" and
// ".json"), each registered converter fires independently. This lets a broad
// ".json" observer coexist with a specific ".script.json" converter.
TEST(AssetManager, CompoundAndBareExtensionBothFire) {
  TempDir dir;
  dir.writeFile("level.scene.json", "{}");
  AssetManager mgr(dir.path());

  int sceneCalls = 0;
  int jsonCalls = 0;
  mgr.addAssetConverter({".scene.json"},
                        [&](const RawAsset&, const AssetHandle&) { ++sceneCalls; });
  mgr.addAssetConverter({".json"},
                        [&](const RawAsset&, const AssetHandle&) { ++jsonCalls; });

  mgr.loadAsset("level.scene.json");
  EXPECT_EQ(sceneCalls, 1);
  EXPECT_EQ(jsonCalls, 1);
}

// A single addAssetConverter call covering multiple extensions fires the
// callback for loads of any of those extensions.
TEST(AssetManager, MultiExtensionRegistrationMatchesAll) {
  TempDir dir;
  dir.writeFile("img.png", "P");
  dir.writeFile("img.jpg", "J");
  AssetManager mgr(dir.path());

  int calls = 0;
  mgr.addAssetConverter({".png", ".jpg"}, [&](const RawAsset&, const AssetHandle&) { ++calls; });

  mgr.loadAsset("img.png");
  mgr.loadAsset("img.jpg");
  EXPECT_EQ(calls, 2);
}

// Multiple converters registered for the same extension all fire on load,
// in registration order. Enables observer-style plugins (e.g. a texture
// module and a thumbnail module both wanting to see .png).
TEST(AssetManager, MultipleConvertersForSameExtensionAllFire) {
  TempDir dir;
  dir.writeFile("a.png", "bytes");
  AssetManager mgr(dir.path());

  int aCalls = 0, bCalls = 0;
  mgr.addAssetConverter({".png"}, [&](const RawAsset&, const AssetHandle&) { ++aCalls; });
  mgr.addAssetConverter({".png"}, [&](const RawAsset&, const AssetHandle&) { ++bCalls; });

  mgr.loadAsset("a.png");

  EXPECT_EQ(aCalls, 1);
  EXPECT_EQ(bCalls, 1);
}

// Extensions are case-normalized so ".PNG" and ".png" hit the same converter.
TEST(AssetManager, ConverterExtensionCaseInsensitive) {
  TempDir dir;
  dir.writeFile("a.PNG", "bytes");
  AssetManager mgr(dir.path());

  int calls = 0;
  mgr.addAssetConverter({".png"}, [&](const RawAsset&, const AssetHandle&) { ++calls; });

  mgr.loadAsset("a.PNG");
  EXPECT_EQ(calls, 1);
}

// A converter that throws is isolated — the load completes, the raw asset is
// retrievable, and other converters for the same extension still fire.
TEST(AssetManager, ConverterThatThrowsDoesNotKillLoadOrOtherConverters) {
  TempDir dir;
  dir.writeFile("a.dat", "payload");
  AssetManager mgr(dir.path());

  int goodCalls = 0;
  mgr.addAssetConverter({".dat"}, [&](const RawAsset&, const AssetHandle&) {
    throw std::runtime_error("bad converter");
  });
  mgr.addAssetConverter({".dat"}, [&](const RawAsset&, const AssetHandle&) { ++goodCalls; });

  AssetHandle handle;
  EXPECT_NO_THROW(handle = mgr.loadAsset("a.dat"));
  EXPECT_TRUE(handle.isValid());
  EXPECT_EQ(goodCalls, 1);

  // Raw asset is still retrievable by the returned handle.
  const RawAsset& asset = mgr.getRawAsset(handle);
  std::string got(asset.data.begin(), asset.data.end());
  EXPECT_EQ(got, "payload");
}
