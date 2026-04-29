#include "Archive.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
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

}  // namespace

Archive Archive::openFile(const std::filesystem::path& path) {
  Archive archive;
  archive._path = path;

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    fail(path, "failed to open archive file");
  }

  const std::streamsize fileSize = file.tellg();
  if (fileSize < 0) {
    fail(path, "failed to determine archive file size");
  }
  if (static_cast<std::size_t>(fileSize) < kHeaderSize) {
    fail(path, "file smaller than archive header (" +
                   std::to_string(fileSize) + " bytes)");
  }

  file.seekg(0, std::ios::beg);
  archive._bytes.resize(static_cast<std::size_t>(fileSize));
  if (!file.read(reinterpret_cast<char*>(archive._bytes.data()), fileSize)) {
    fail(path, "failed to read archive bytes");
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
