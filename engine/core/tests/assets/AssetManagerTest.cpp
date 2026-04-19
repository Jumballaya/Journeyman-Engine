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

// Loading the same path twice produces two distinct handles — pins the
// current no-dedupe behavior so that a future caching change surfaces
// intentionally rather than silently.
TEST(AssetManager, LoadingSameFileTwiceReturnsDistinctHandles) {
  TempDir dir;
  dir.writeFile("data.txt", "x");
  AssetManager mgr(dir.path());

  auto h1 = mgr.loadAsset("data.txt");
  auto h2 = mgr.loadAsset("data.txt");
  EXPECT_NE(h1, h2);
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
