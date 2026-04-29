#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "Archive.hpp"
#include "TempDir.hpp"

namespace {

// Mirrors archive::AssetEntry from the Go writer. Tests construct a list of
// these, hand them to buildArchiveBytes(), and write the result to a TempDir.
struct TestEntry {
  std::string path;
  std::string type;
  nlohmann::json metadata = nlohmann::json::object();
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

// Build a well-formed JMA1 archive byte stream from the given entries. Used by
// every roundtrip test below; the malformed-archive tests construct bytes by
// hand so they can corrupt specific fields.
std::vector<std::uint8_t> buildArchiveBytes(const std::vector<TestEntry>& entries) {
  nlohmann::json resolver = nlohmann::json::object();
  std::vector<std::uint8_t> payload;
  std::uint64_t offset = 0;
  for (const auto& e : entries) {
    nlohmann::json entry = nlohmann::json::object();
    entry["offset"] = offset;
    entry["size"] = e.payload.size();
    entry["type"] = e.type;
    entry["metadata"] = e.metadata;
    resolver[e.path] = entry;
    payload.insert(payload.end(), e.payload.begin(), e.payload.end());
    offset += e.payload.size();
  }
  const std::string resolverStr = resolver.dump();

  const std::uint64_t payloadOffset = Archive::kHeaderSize;
  const std::uint64_t payloadSize = payload.size();
  const std::uint64_t resolverOffset = payloadOffset + payloadSize;

  std::vector<std::uint8_t> bytes;
  bytes.reserve(Archive::kHeaderSize + payloadSize + resolverStr.size());
  putU32(bytes, Archive::kMagic);
  putU32(bytes, Archive::kVersion);
  putU64(bytes, payloadOffset);
  putU64(bytes, payloadSize);
  putU64(bytes, resolverOffset);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.insert(bytes.end(), resolverStr.begin(), resolverStr.end());
  return bytes;
}

std::filesystem::path writeArchive(const TempDir& dir,
                                   const std::string& name,
                                   const std::vector<TestEntry>& entries) {
  auto bytes = buildArchiveBytes(entries);
  dir.writeFile(name, bytes);
  return dir.path() / name;
}

}  // namespace

// ---------------------------------------------------------------------------
// Roundtrip cases: Archive::openFile + read/contains/typeOf/metadataOf

// An archive with zero entries has an empty resolver. contains() reports false
// for every path; openFile must not reject the empty case.
TEST(Archive, EmptyArchiveRoundtrip) {
  TempDir dir;
  auto archivePath = writeArchive(dir, "empty.jm", {});

  Archive archive = Archive::openFile(archivePath);
  EXPECT_FALSE(archive.contains("anything"));
  EXPECT_FALSE(archive.contains(""));
}

// Single entry: contains() returns true and read() returns byte-identical
// payload along with the queried source path.
TEST(Archive, SingleEntryRoundtrip) {
  TempDir dir;
  std::vector<std::uint8_t> payload = {0x10, 0x20, 0x30, 0x40};
  auto archivePath = writeArchive(dir, "one.jm", {{"foo.bin", "image", {}, payload}});

  Archive archive = Archive::openFile(archivePath);
  EXPECT_TRUE(archive.contains("foo.bin"));
  RawAsset asset = archive.read("foo.bin");
  EXPECT_EQ(asset.data, payload);
  EXPECT_EQ(asset.filePath.generic_string(), "foo.bin");
}

// Multiple entries with different types and metadata round-trip. JSON object
// key ordering is intentionally not asserted — the contract is set membership,
// not iteration order.
TEST(Archive, MultipleEntriesRoundtripPreservesContent) {
  TempDir dir;
  std::vector<TestEntry> entries = {
      {"a.bin", "image", {}, {0xA0, 0xA1}},
      {"b.bin", "audio", {{"sampleRate", 44100}}, {0xB0, 0xB1, 0xB2}},
      {"scripts/c.bin", "script", {{"imports", {"abort", "log"}}}, {0xC0}},
      {"d.bin", "scene", {}, {0xD0, 0xD1, 0xD2, 0xD3, 0xD4}},
      {"e.bin", "manifest", {}, {0xE0, 0xE1}},
  };
  auto archivePath = writeArchive(dir, "multi.jm", entries);

  Archive archive = Archive::openFile(archivePath);
  for (const auto& e : entries) {
    EXPECT_TRUE(archive.contains(e.path)) << e.path;
    EXPECT_EQ(archive.read(e.path).data, e.payload) << e.path;
    auto t = archive.typeOf(e.path);
    ASSERT_TRUE(t.has_value()) << e.path;
    EXPECT_EQ(std::string(*t), e.type) << e.path;
    auto m = archive.metadataOf(e.path);
    ASSERT_TRUE(m.has_value()) << e.path;
    EXPECT_EQ(*m, e.metadata) << e.path;
  }
}

// Binary payloads containing zero bytes, high bytes, and pseudo-multibyte
// sequences round-trip exactly. Pins the binary-safe payload contract — the
// archive must not interpret payload contents as text.
TEST(Archive, EntryWithBinaryPayload) {
  TempDir dir;
  std::vector<std::uint8_t> payload = {
      0x00, 0xFF, 0x00, 0x01, 0xE2, 0x9C, 0x93, 0x00, 0x80, 0x7F};
  auto archivePath = writeArchive(dir, "bin.jm", {{"blob.dat", "image", {}, payload}});

  Archive archive = Archive::openFile(archivePath);
  EXPECT_EQ(archive.read("blob.dat").data, payload);
}

// contains() returns false for an unknown path; read() throws with the path in
// the error message so logs are grep-able.
TEST(Archive, MissingPathReturnsFalseFromContains) {
  TempDir dir;
  auto archivePath = writeArchive(
      dir, "miss.jm", {{"present.bin", "image", {}, {0x01, 0x02}}});

  Archive archive = Archive::openFile(archivePath);
  EXPECT_FALSE(archive.contains("nope.bin"));
  EXPECT_FALSE(archive.typeOf("nope.bin").has_value());
  EXPECT_FALSE(archive.metadataOf("nope.bin").has_value());

  try {
    archive.read("nope.bin");
    FAIL() << "expected read() to throw on missing entry";
  } catch (const std::runtime_error& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("nope.bin"), std::string::npos) << what;
  }
}

// 100+ entries with ~1MB total payload round-trip. Spot-checks a deterministic
// sample rather than every byte to keep the test fast.
TEST(Archive, LargeArchiveRoundtrip) {
  TempDir dir;
  std::vector<TestEntry> entries;
  std::mt19937 rng(0xC0FFEE);
  for (int i = 0; i < 128; ++i) {
    TestEntry e;
    e.path = "asset_" + std::to_string(i) + ".bin";
    e.type = "image";
    const std::size_t size = 8 * 1024;
    e.payload.resize(size);
    for (auto& b : e.payload) b = static_cast<std::uint8_t>(rng() & 0xFF);
    entries.push_back(std::move(e));
  }
  auto archivePath = writeArchive(dir, "large.jm", entries);

  Archive archive = Archive::openFile(archivePath);
  for (int idx : {0, 1, 17, 64, 99, 127}) {
    EXPECT_EQ(archive.read(entries[idx].path).data, entries[idx].payload)
        << "mismatch at idx=" << idx;
  }
}

// Opening then dropping an Archive releases the underlying buffer. Verified
// indirectly by re-opening many archives in sequence — if the destructor leaks
// the file handle or buffer, a sanitizer-clean build still flags the test.
TEST(Archive, ArchiveDestructorReleasesMemory) {
  TempDir dir;
  auto archivePath = writeArchive(
      dir, "drop.jm", {{"foo.bin", "image", {}, {0x01, 0x02, 0x03, 0x04}}});

  for (int i = 0; i < 100; ++i) {
    Archive archive = Archive::openFile(archivePath);
    EXPECT_TRUE(archive.contains("foo.bin"));
  }
}

// ---------------------------------------------------------------------------
// Malformed archive cases — each constructs bytes by hand and verifies that
// openFile throws std::runtime_error with a distinguishing message.

// Zero-length file is rejected before any header parse.
TEST(Archive, MalformedThrowsOnEmptyFile) {
  TempDir dir;
  dir.writeFile("empty.jm", std::vector<std::uint8_t>{});
  EXPECT_THROW(Archive::openFile(dir.path() / "empty.jm"), std::runtime_error);
}

// File shorter than the 32-byte header is rejected.
TEST(Archive, MalformedThrowsOnFileTooShort) {
  TempDir dir;
  dir.writeFile("short.jm", std::vector<std::uint8_t>(16, 0));
  EXPECT_THROW(Archive::openFile(dir.path() / "short.jm"), std::runtime_error);
}

// Magic bytes mismatch is rejected. Build a header where every other field is
// valid so the only failure path is the magic check.
TEST(Archive, MalformedThrowsOnWrongMagic) {
  TempDir dir;
  std::vector<std::uint8_t> bytes;
  putU32(bytes, 0xDEADBEEF);  // wrong magic
  putU32(bytes, Archive::kVersion);
  putU64(bytes, Archive::kHeaderSize);
  putU64(bytes, 0);
  putU64(bytes, Archive::kHeaderSize);
  bytes.insert(bytes.end(), {'{', '}'});  // valid empty resolver
  dir.writeFile("badmagic.jm", bytes);
  EXPECT_THROW(Archive::openFile(dir.path() / "badmagic.jm"), std::runtime_error);
}

// Version mismatch is rejected. Bumping the format version must force a
// re-pack rather than silently load an older archive.
TEST(Archive, MalformedThrowsOnWrongVersion) {
  TempDir dir;
  std::vector<std::uint8_t> bytes;
  putU32(bytes, Archive::kMagic);
  putU32(bytes, 99);  // unsupported version
  putU64(bytes, Archive::kHeaderSize);
  putU64(bytes, 0);
  putU64(bytes, Archive::kHeaderSize);
  bytes.insert(bytes.end(), {'{', '}'});
  dir.writeFile("badver.jm", bytes);
  EXPECT_THROW(Archive::openFile(dir.path() / "badver.jm"), std::runtime_error);
}

// payload_offset + payload_size != resolver_offset is corrupt header.
TEST(Archive, MalformedThrowsOnHeaderInconsistency) {
  TempDir dir;
  std::vector<std::uint8_t> bytes;
  putU32(bytes, Archive::kMagic);
  putU32(bytes, Archive::kVersion);
  putU64(bytes, Archive::kHeaderSize);
  putU64(bytes, 4);                        // payload_size says 4 bytes
  putU64(bytes, Archive::kHeaderSize);     // but resolver_offset is right after header
  bytes.insert(bytes.end(), {'{', '}'});
  dir.writeFile("badhdr.jm", bytes);
  EXPECT_THROW(Archive::openFile(dir.path() / "badhdr.jm"), std::runtime_error);
}

// resolver_offset past end of file is corrupt.
TEST(Archive, MalformedThrowsOnResolverOffsetPastEOF) {
  TempDir dir;
  std::vector<std::uint8_t> bytes;
  putU32(bytes, Archive::kMagic);
  putU32(bytes, Archive::kVersion);
  putU64(bytes, Archive::kHeaderSize);
  putU64(bytes, 0);
  putU64(bytes, Archive::kHeaderSize + 1024);  // claims resolver starts past EOF
  // No payload, no resolver bytes at all.
  dir.writeFile("badoff.jm", bytes);
  EXPECT_THROW(Archive::openFile(dir.path() / "badoff.jm"), std::runtime_error);
}

// Resolver section that isn't valid JSON is rejected.
TEST(Archive, MalformedThrowsOnBadResolverJson) {
  TempDir dir;
  std::vector<std::uint8_t> bytes;
  putU32(bytes, Archive::kMagic);
  putU32(bytes, Archive::kVersion);
  putU64(bytes, Archive::kHeaderSize);
  putU64(bytes, 0);
  putU64(bytes, Archive::kHeaderSize);
  const std::string garbage = "this is not json {";
  bytes.insert(bytes.end(), garbage.begin(), garbage.end());
  dir.writeFile("badjson.jm", bytes);
  EXPECT_THROW(Archive::openFile(dir.path() / "badjson.jm"), std::runtime_error);
}

// A resolver entry whose offset+size points past the end of the payload
// section is rejected; otherwise read() would read past payload bounds.
TEST(Archive, MalformedThrowsOnEntryOutOfBounds) {
  TempDir dir;
  // Construct: header points at a 4-byte payload, resolver claims a 100-byte
  // entry starting at offset 0. openFile must catch the mismatch.
  std::vector<std::uint8_t> payload = {0x01, 0x02, 0x03, 0x04};
  nlohmann::json resolver = {
      {"oversized.bin", {{"offset", 0}, {"size", 100}, {"type", "image"}, {"metadata", nlohmann::json::object()}}},
  };
  const std::string resolverStr = resolver.dump();

  std::vector<std::uint8_t> bytes;
  putU32(bytes, Archive::kMagic);
  putU32(bytes, Archive::kVersion);
  putU64(bytes, Archive::kHeaderSize);
  putU64(bytes, payload.size());
  putU64(bytes, Archive::kHeaderSize + payload.size());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.insert(bytes.end(), resolverStr.begin(), resolverStr.end());
  dir.writeFile("oob.jm", bytes);
  EXPECT_THROW(Archive::openFile(dir.path() / "oob.jm"), std::runtime_error);
}
