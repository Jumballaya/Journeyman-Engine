#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "RawAsset.hpp"

// The manifest's key inside an archive; jm pack/run use the same string.
inline constexpr std::string_view kManifestEntryKey = ".jm.json";

// Read-only, in-memory view of a packed .jm archive (layout: docs/content.md,
// "Archive format"). Immutable after openFile, so safe from any thread.
class Archive {
 public:
  static constexpr std::uint32_t kMagic = 0x31414D4A;
  static constexpr std::uint32_t kVersion = 1;
  static constexpr std::size_t kHeaderSize = 32;

  // Throws std::runtime_error on any malformed header, resolver or entry.
  static Archive openFile(const std::filesystem::path& path);

  Archive() = default;
  ~Archive() = default;
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;
  Archive(Archive&&) noexcept = default;
  Archive& operator=(Archive&&) noexcept = default;

  bool contains(std::string_view sourcePath) const;

  // A copy of the entry's bytes; throws if the path isn't in the archive.
  RawAsset read(std::string_view sourcePath) const;

  std::optional<std::string_view> typeOf(std::string_view sourcePath) const;
  std::optional<nlohmann::json> metadataOf(std::string_view sourcePath) const;

 private:
  struct Entry {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::string type;
    nlohmann::json metadata;
  };

  std::filesystem::path _path;
  std::vector<std::uint8_t> _bytes;
  std::uint64_t _payloadOffset = 0;
  std::uint64_t _payloadSize = 0;
  std::unordered_map<std::string, Entry> _entries;
};
