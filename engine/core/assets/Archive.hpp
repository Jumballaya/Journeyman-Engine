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

// Archive: read-only view over a Journeyman Engine archive file ("JMA1").
//
// On-disk layout (see project-E plan, Locked design decisions):
//   [Header — 32 bytes]
//     u32 magic = 'J','M','A','1' (0x31414D4A LE)
//     u32 version = 1
//     u64 payload_offset
//     u64 payload_size
//     u64 resolver_offset (= payload_offset + payload_size)
//   [Payload section — variable]
//     Concatenated raw blobs. Random access via (offset, size) from resolver.
//   [Resolver section — variable]
//     Single UTF-8 JSON object keyed by canonical source path.
//
// Implementation reads the whole file into memory at openFile() and parses the
// resolver into an in-memory map. read() returns a RawAsset with copied bytes.
// Read-only after construction; safe to call from any thread.
// Resolver key under which the game manifest is stored inside a packed
// archive. Both the engine (Application::run, archive branch) and the CLI
// (jm pack writer, jm run archive reader) reference this exact string —
// keep them in sync via this constant. Folder mode uses the same name as
// the manifest's filename on disk by convention.
inline constexpr std::string_view kManifestEntryKey = ".jm.json";

class Archive {
 public:
  static constexpr std::uint32_t kMagic = 0x31414D4A;
  static constexpr std::uint32_t kVersion = 1;
  static constexpr std::size_t kHeaderSize = 32;

  // Opens and validates an archive file. Throws std::runtime_error on any
  // malformation (bad magic, unsupported version, header inconsistency,
  // resolver JSON parse failure, entry offset/size out of bounds).
  static Archive openFile(const std::filesystem::path& path);

  Archive() = default;
  ~Archive() = default;
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;
  Archive(Archive&&) noexcept = default;
  Archive& operator=(Archive&&) noexcept = default;

  bool contains(std::string_view sourcePath) const;

  // Returns a RawAsset whose data is a copy of the entry's payload. Throws
  // std::runtime_error if the path is not present in the resolver.
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
