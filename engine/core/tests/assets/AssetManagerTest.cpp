#include <gtest/gtest.h>

#include <cstdint>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "Archive.hpp"
#include "AssetManager.hpp"
#include "TempDir.hpp"

namespace {

// Minimal JMA1 archive builder for archive-backed AssetManager tests. Mirrors
// the helper in ArchiveTest.cpp; kept local rather than shared because the
// two test files exercise different layers and either may evolve separately.
struct ArchiveTestEntry {
  std::string path;
  std::string type;
  std::vector<std::uint8_t> payload;
};

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

void putU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

std::filesystem::path writeFixtureArchive(const TempDir& dir,
                                          const std::string& name,
                                          const std::vector<ArchiveTestEntry>& entries) {
  nlohmann::json resolver = nlohmann::json::object();
  std::vector<std::uint8_t> payload;
  std::uint64_t offset = 0;
  for (const auto& e : entries) {
    nlohmann::json entry = nlohmann::json::object();
    entry["offset"] = offset;
    entry["size"] = e.payload.size();
    entry["type"] = e.type;
    entry["metadata"] = nlohmann::json::object();
    resolver[e.path] = entry;
    payload.insert(payload.end(), e.payload.begin(), e.payload.end());
    offset += e.payload.size();
  }
  const std::string resolverStr = resolver.dump();

  std::vector<std::uint8_t> bytes;
  putU32(bytes, Archive::kMagic);
  putU32(bytes, Archive::kVersion);
  putU64(bytes, Archive::kHeaderSize);
  putU64(bytes, payload.size());
  putU64(bytes, Archive::kHeaderSize + payload.size());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.insert(bytes.end(), resolverStr.begin(), resolverStr.end());

  dir.writeFile(name, bytes);
  return dir.path() / name;
}

}  // namespace

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

// ---------------------------------------------------------------------------
// Archive-backed AssetManager (E.2). Constructing AssetManager with a path
// that names a regular file with a .jm extension routes through the archive
// backend. The public API surface is identical to folder mode — the only
// difference is where bytes come from.

// loadAsset reads the entry's payload from the archive. Pins the basic
// archive-mode round-trip.
TEST(AssetManager, LoadAssetFromArchiveBackend) {
  TempDir dir;
  std::vector<std::uint8_t> payload = {0x10, 0x20, 0x30, 0x40};
  auto archivePath = writeFixtureArchive(dir, "game.jm",
                                         {{"foo.bin", "image", payload}});

  AssetManager mgr(archivePath);
  auto handle = mgr.loadAsset("foo.bin");
  ASSERT_TRUE(handle.isValid());

  const RawAsset& asset = mgr.getRawAsset(handle);
  EXPECT_EQ(asset.data, payload);
}

// Converters registered by extension still fire when the bytes came from an
// archive — the converter dispatch is path-based, independent of which backend
// produced the bytes.
TEST(AssetManager, ConverterFiresForArchiveLoadedAsset) {
  TempDir dir;
  std::string contents = "noted";
  std::vector<std::uint8_t> payload(contents.begin(), contents.end());
  auto archivePath = writeFixtureArchive(dir, "game.jm",
                                         {{"note.txt", "text", payload}});

  AssetManager mgr(archivePath);
  int calls = 0;
  RawAsset capturedAsset;
  mgr.addAssetConverter({".txt"}, [&](const RawAsset& asset, const AssetHandle&) {
    ++calls;
    capturedAsset = asset;
  });

  mgr.loadAsset("note.txt");
  EXPECT_EQ(calls, 1);
  std::string got(capturedAsset.data.begin(), capturedAsset.data.end());
  EXPECT_EQ(got, contents);
}

// Path-keyed dedup applies to archive-backed loads too: a repeated load
// returns the cached handle without re-fetching from the archive or re-firing
// converters.
TEST(AssetManager, LoadingSamePathTwiceReturnsSameHandleArchiveBackend) {
  TempDir dir;
  std::vector<std::uint8_t> payload = {0x01, 0x02, 0x03};
  auto archivePath = writeFixtureArchive(dir, "game.jm",
                                         {{"data.bin", "image", payload}});

  AssetManager mgr(archivePath);
  int calls = 0;
  mgr.addAssetConverter({".bin"}, [&](const RawAsset&, const AssetHandle&) { ++calls; });

  auto h1 = mgr.loadAsset("data.bin");
  auto h2 = mgr.loadAsset("data.bin");
  EXPECT_EQ(h1, h2);
  EXPECT_EQ(calls, 1);
}

// Archive-backed loads resolve through the archive's resolver — the asset's
// source path need not exist on disk. Pins that folder-mode and archive-mode
// are mutually exclusive: mounting an archive does not also fall back to disk.
TEST(AssetManager, LoadingFromArchiveDoesNotTouchDisk) {
  TempDir dir;
  std::vector<std::uint8_t> payload = {0xAA, 0xBB};
  auto archivePath = writeFixtureArchive(
      dir, "game.jm", {{"assets/scripts/player.ts", "script", payload}});

  // Sanity: the archive-internal path does NOT exist on disk under the temp
  // dir. If the archive backend secretly fell back to the folder, the load
  // would fail with "file not found"; instead it should succeed via the
  // resolver.
  ASSERT_FALSE(std::filesystem::exists(dir.path() / "assets/scripts/player.ts"));

  AssetManager mgr(archivePath);
  auto handle = mgr.loadAsset("assets/scripts/player.ts");
  ASSERT_TRUE(handle.isValid());
  EXPECT_EQ(mgr.getRawAsset(handle).data, payload);
}
