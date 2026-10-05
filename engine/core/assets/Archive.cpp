#include "Archive.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <functional>
#include <span>
#include <vector>
#include <stdexcept>
#include <string>

#include "../logger/logging.hpp"

namespace {

std::uint32_t readU32LE(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t readU64LE(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return v;
}

[[noreturn]] void fail(const std::filesystem::path& path, const std::string& reason) {
  const std::string msg = "[Archive] " + path.string() + ": " + reason;
  JM_LOG_ERROR("{}", msg);
  throw std::runtime_error(msg);
}

// How far back from the end to look: a signature hashes every page, ~1/128 of the file.
std::size_t footerSearchWindow(std::uint64_t fileSize) {
  return static_cast<std::size_t>(std::min<std::uint64_t>(fileSize, fileSize / 64 + (1u << 20)));
}

struct Embedded {
  std::uint64_t begin, end;  // the archive's bytes within the file
};

// An archive appended to an executable. Its footer ends the file, or sits
// before a code signature added after it (macOS), so it is searched for
// backwards in `tail` (the file's last bytes); `magicAt` reads the 4 bytes at
// a file offset, to confirm an archive starts there.
std::optional<Embedded> locateEmbedded(std::span<const std::uint8_t> tail, std::uint64_t fileSize,
                                       const std::function<std::uint32_t(std::uint64_t)>& magicAt) {
  const std::uint64_t base = fileSize - tail.size();
  for (std::size_t end = tail.size(); end >= Archive::kEmbedFooterSize; --end) {
    const std::uint8_t* footer = tail.data() + end - Archive::kEmbedFooterSize;
    if (std::memcmp(footer + 8, Archive::kEmbedMagic, 8) != 0) continue;
    const std::uint64_t begin = readU64LE(footer);
    const std::uint64_t archiveEnd = base + end - Archive::kEmbedFooterSize;
    if (begin + Archive::kHeaderSize <= archiveEnd && magicAt(begin) == Archive::kMagic) return Embedded{begin, archiveEnd};
  }
  return std::nullopt;
}

}  // namespace

bool Archive::isEmbeddedIn(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;
  const auto size = static_cast<std::uint64_t>(file.tellg());
  std::vector<std::uint8_t> tail(footerSearchWindow(size));
  file.seekg(static_cast<std::streamoff>(size - tail.size()));
  if (!file.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()))) return false;
  return locateEmbedded(tail, size, [&file](std::uint64_t offset) -> std::uint32_t {
           std::uint8_t magic[4] = {};
           file.clear();
           file.seekg(static_cast<std::streamoff>(offset));
           file.read(reinterpret_cast<char*>(magic), 4);
           return readU32LE(magic);
         }).has_value();
}

Archive Archive::openFile(const std::filesystem::path& path) {
  Archive archive;
  archive._path = path;

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    fail(path, "failed to open archive file");
  }

  const std::streamsize totalSize = file.tellg();
  if (totalSize < 0) {
    fail(path, "failed to determine archive file size");
  }
  file.seekg(0, std::ios::beg);
  archive._bytes.resize(static_cast<std::size_t>(totalSize));
  if (!file.read(reinterpret_cast<char*>(archive._bytes.data()), totalSize)) {
    fail(path, "failed to read archive bytes");
  }

  // An executable with a game appended: keep only the archive's bytes.
  auto& bytes = archive._bytes;
  if (bytes.size() >= kHeaderSize && readU32LE(bytes.data()) != kMagic) {
    const std::span<const std::uint8_t> tail(bytes.data() + bytes.size() - footerSearchWindow(bytes.size()),
                                             footerSearchWindow(bytes.size()));
    auto embedded = locateEmbedded(tail, bytes.size(), [&bytes](std::uint64_t offset) { return readU32LE(bytes.data() + offset); });
    if (embedded) {
      bytes.resize(embedded->end);
      bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(embedded->begin));
    }
  }
  const auto fileSize = static_cast<std::streamsize>(archive._bytes.size());
  if (static_cast<std::size_t>(fileSize) < kHeaderSize) {
    fail(path, "file smaller than archive header (" +
                   std::to_string(fileSize) + " bytes)");
  }

  const std::uint8_t* hdr = archive._bytes.data();
  const std::uint32_t magic = readU32LE(hdr + 0);
  if (magic != kMagic) {
    fail(path, "bad magic: expected JMA1, got 0x" + [&] {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%08X", magic);
      return std::string(buf);
    }());
  }

  const std::uint32_t version = readU32LE(hdr + 4);
  if (version != kVersion) {
    fail(path, "unsupported archive version: " + std::to_string(version) +
                   " (expected " + std::to_string(kVersion) + ")");
  }

  const std::uint64_t payloadOffset = readU64LE(hdr + 8);
  const std::uint64_t payloadSize = readU64LE(hdr + 16);
  const std::uint64_t resolverOffset = readU64LE(hdr + 24);

  if (payloadOffset != kHeaderSize) {
    fail(path, "payload_offset (" + std::to_string(payloadOffset) +
                   ") must equal header size (" + std::to_string(kHeaderSize) + ")");
  }
  if (payloadOffset + payloadSize != resolverOffset) {
    fail(path, "header inconsistency: payload_offset(" +
                   std::to_string(payloadOffset) + ") + payload_size(" +
                   std::to_string(payloadSize) + ") != resolver_offset(" +
                   std::to_string(resolverOffset) + ")");
  }
  if (resolverOffset > static_cast<std::uint64_t>(fileSize)) {
    fail(path, "resolver_offset (" + std::to_string(resolverOffset) +
                   ") past end of file (" + std::to_string(fileSize) + ")");
  }

  archive._payloadOffset = payloadOffset;
  archive._payloadSize = payloadSize;

  const std::size_t resolverLen =
      static_cast<std::size_t>(fileSize) - static_cast<std::size_t>(resolverOffset);
  std::string_view resolverView(
      reinterpret_cast<const char*>(archive._bytes.data() + resolverOffset),
      resolverLen);

  nlohmann::json resolver;
  try {
    resolver = nlohmann::json::parse(resolverView);
  } catch (const nlohmann::json::parse_error& e) {
    fail(path, std::string("malformed resolver JSON: ") + e.what());
  }

  if (!resolver.is_object()) {
    fail(path, "resolver root must be a JSON object");
  }

  archive._entries.reserve(resolver.size());
  for (auto it = resolver.begin(); it != resolver.end(); ++it) {
    const std::string& key = it.key();
    const nlohmann::json& v = it.value();
    if (!v.is_object()) {
      fail(path, "resolver entry '" + key + "' is not a JSON object");
    }
    if (!v.contains("offset") || !v.contains("size")) {
      fail(path, "resolver entry '" + key + "' missing offset/size");
    }

    Entry entry;
    entry.offset = v.at("offset").get<std::uint64_t>();
    entry.size = v.at("size").get<std::uint64_t>();
    if (v.contains("type") && v.at("type").is_string()) {
      entry.type = v.at("type").get<std::string>();
    }
    if (v.contains("metadata")) {
      entry.metadata = v.at("metadata");
    } else {
      entry.metadata = nlohmann::json::object();
    }

    if (entry.offset > payloadSize ||
        entry.size > payloadSize - entry.offset) {
      fail(path, "resolver entry '" + key + "' out of bounds (offset=" +
                     std::to_string(entry.offset) + ", size=" +
                     std::to_string(entry.size) + ", payload_size=" +
                     std::to_string(payloadSize) + ")");
    }

    archive._entries.emplace(key, std::move(entry));
  }

  return archive;
}

bool Archive::contains(std::string_view sourcePath) const {
  return _entries.find(std::string(sourcePath)) != _entries.end();
}

RawAsset Archive::read(std::string_view sourcePath) const {
  auto it = _entries.find(std::string(sourcePath));
  if (it == _entries.end()) {
    fail(_path, "no such entry: '" + std::string(sourcePath) + "'");
  }
  const Entry& entry = it->second;
  RawAsset asset;
  asset.filePath = std::filesystem::path(std::string(sourcePath));
  asset.data.resize(entry.size);
  if (entry.size > 0) {
    std::memcpy(asset.data.data(),
                _bytes.data() + _payloadOffset + entry.offset,
                entry.size);
  }
  return asset;
}

std::optional<std::string_view> Archive::typeOf(std::string_view sourcePath) const {
  auto it = _entries.find(std::string(sourcePath));
  if (it == _entries.end()) return std::nullopt;
  if (it->second.type.empty()) return std::nullopt;
  return std::string_view(it->second.type);
}

std::optional<nlohmann::json> Archive::metadataOf(std::string_view sourcePath) const {
  auto it = _entries.find(std::string(sourcePath));
  if (it == _entries.end()) return std::nullopt;
  return it->second.metadata;
}
