#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "Archive.hpp"

// Reads assets by manifest-relative path from a mounted folder (the default:
// ".") or a .jm archive.
class FileSystem {
 public:
  // The form asset paths are compared in: "./foo" and "foo" match, as in the
  // archive resolver.
  static std::string key(const std::filesystem::path& p) { return p.lexically_normal().generic_string(); }

  bool exists(const std::filesystem::path& filePath) const;
  std::vector<uint8_t> read(const std::filesystem::path& filePath) const;

  void mountFolder(const std::filesystem::path& folderPath);
  void mountArchive(const std::filesystem::path& archivePath);

  // An archive entry's type; nullopt in folder mode (dispatch uses the extension).
  std::optional<std::string> typeOf(const std::filesystem::path& assetPath) const;
  std::optional<nlohmann::json> metadataOf(const std::filesystem::path& assetPath) const;

 private:
  std::filesystem::path _folder = ".";
  std::optional<Archive> _archive;  // set in archive mode
};
