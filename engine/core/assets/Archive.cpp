#include "Archive.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "../logger/logging.hpp"

namespace {

// A game appended to an executable ends with this footer: the archive's
// offset (u64 LE), then kEmbedMagic.
constexpr char kEmbedMagic[8] = {'J', 'M', 'G', 'A', 'M', 'E', '0', '1'};
constexpr std::size_t kEmbedFooterSize = 16;

template <typename T>
T readLE(const std::uint8_t* p) {
  T v = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) v |= static_cast<T>(p[i]) << (8 * i);
  return v;
}

template <typename... Args>
[[noreturn]] void fail(const std::filesystem::path& path, spdlog::format_string_t<Args...> reason, Args&&... args) {
  const std::string msg =
      "[Archive] " + path.string() + ": " + spdlog::fmt_lib::format(reason, std::forward<Args>(args)...);
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
  for (std::size_t end = tail.size(); end >= kEmbedFooterSize; --end) {
    const std::uint8_t* footer = tail.data() + end - kEmbedFooterSize;
    if (std::memcmp(footer + 8, kEmbedMagic, 8) != 0) continue;
    const auto begin = readLE<std::uint64_t>(footer);
    const std::uint64_t archiveEnd = base + end - kEmbedFooterSize;
    // Subtracting, not adding: a garbage offset must not wrap past the check.
    if (begin <= archiveEnd && archiveEnd - begin >= Archive::kHeaderSize && magicAt(begin) == Archive::kMagic) {
      return Embedded{begin, archiveEnd};
    }
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
  return locateEmbedded(tail, size, [&file](std::uint64_t offset) {
           std::uint8_t magic[4] = {};
           file.clear();
           file.seekg(static_cast<std::streamoff>(offset));
           file.read(reinterpret_cast<char*>(magic), 4);
           return readLE<std::uint32_t>(magic);
         }).has_value();
}

Archive Archive::openFile(const std::filesystem::path& path) {
  Archive archive;
  archive._path = path;
  auto& bytes = archive._bytes;

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) fail(path, "failed to open archive file");
  const std::streamsize totalSize = file.tellg();
  if (totalSize < 0) fail(path, "failed to determine archive file size");
  file.seekg(0, std::ios::beg);
  bytes.resize(static_cast<std::size_t>(totalSize));
  if (!file.read(reinterpret_cast<char*>(bytes.data()), totalSize)) fail(path, "failed to read archive bytes");

  // An executable with a game appended: keep only the archive's bytes.
  if (bytes.size() >= kHeaderSize && readLE<std::uint32_t>(bytes.data()) != kMagic) {
    const std::size_t window = footerSearchWindow(bytes.size());
    const std::span<const std::uint8_t> tail(bytes.data() + bytes.size() - window, window);
    auto magicAt = [&bytes](std::uint64_t offset) { return readLE<std::uint32_t>(bytes.data() + offset); };
    if (auto embedded = locateEmbedded(tail, bytes.size(), magicAt)) {
      bytes.resize(embedded->end);
      bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(embedded->begin));
    }
  }
  const std::uint64_t fileSize = bytes.size();
  if (fileSize < kHeaderSize) fail(path, "file smaller than archive header ({} bytes)", fileSize);

  const std::uint8_t* hdr = bytes.data();
  if (const auto magic = readLE<std::uint32_t>(hdr); magic != kMagic) {
    fail(path, "bad magic: expected JMA1, got 0x{:08X}", magic);
  }
  if (const auto version = readLE<std::uint32_t>(hdr + 4); version != kVersion) {
    fail(path, "unsupported archive version: {} (expected {})", version, kVersion);
  }

  const auto payloadOffset = readLE<std::uint64_t>(hdr + 8);
  const auto payloadSize = readLE<std::uint64_t>(hdr + 16);
  const auto resolverOffset = readLE<std::uint64_t>(hdr + 24);
  if (payloadOffset != kHeaderSize) {
    fail(path, "payload_offset ({}) must equal header size ({})", payloadOffset, kHeaderSize);
  }
  if (resolverOffset < kHeaderSize || resolverOffset - kHeaderSize != payloadSize) {
    fail(path, "header inconsistency: payload_offset({}) + payload_size({}) != resolver_offset({})", payloadOffset,
         payloadSize, resolverOffset);
  }
  if (resolverOffset > fileSize) fail(path, "resolver_offset ({}) past end of file ({})", resolverOffset, fileSize);

  nlohmann::json resolver;
  try {
    resolver = nlohmann::json::parse(bytes.begin() + static_cast<std::ptrdiff_t>(resolverOffset), bytes.end());
  } catch (const nlohmann::json::parse_error& e) {
    fail(path, "malformed resolver JSON: {}", e.what());
  }
  if (!resolver.is_object()) fail(path, "resolver root must be a JSON object");

  archive._entries.reserve(resolver.size());
  for (const auto& [key, v] : resolver.items()) {
    if (!v.is_object()) fail(path, "resolver entry '{}' is not a JSON object", key);
    const auto offset = v.find("offset");
    const auto size = v.find("size");
    if (offset == v.end() || size == v.end() || !offset->is_number_unsigned() || !size->is_number_unsigned()) {
      fail(path, "resolver entry '{}' missing offset/size", key);
    }
    Entry entry{offset->get<std::uint64_t>(), size->get<std::uint64_t>(), {},
                v.value("metadata", nlohmann::json::object())};
    if (entry.offset > payloadSize || entry.size > payloadSize - entry.offset) {
      fail(path, "resolver entry '{}' out of bounds (offset={}, size={}, payload_size={})", key, entry.offset,
           entry.size, payloadSize);
    }
    entry.offset += kHeaderSize;
    if (const auto type = v.find("type"); type != v.end() && type->is_string()) entry.type = type->get<std::string>();
    archive._entries.emplace(key, std::move(entry));
  }
  return archive;
}

const Archive::Entry* Archive::find(std::string_view sourcePath) const {
  auto it = _entries.find(std::string(sourcePath));
  return it == _entries.end() ? nullptr : &it->second;
}

bool Archive::contains(std::string_view sourcePath) const { return find(sourcePath) != nullptr; }

RawAsset Archive::read(std::string_view sourcePath) const {
  const Entry* entry = find(sourcePath);
  if (!entry) fail(_path, "no such entry: '{}'", sourcePath);
  const auto* begin = _bytes.data() + entry->offset;
  return RawAsset{std::vector<std::uint8_t>(begin, begin + entry->size), std::filesystem::path(sourcePath)};
}

std::optional<std::string_view> Archive::typeOf(std::string_view sourcePath) const {
  const Entry* entry = find(sourcePath);
  if (!entry || entry->type.empty()) return std::nullopt;
  return entry->type;
}

std::optional<nlohmann::json> Archive::metadataOf(std::string_view sourcePath) const {
  if (const Entry* entry = find(sourcePath)) return entry->metadata;
  return std::nullopt;
}
