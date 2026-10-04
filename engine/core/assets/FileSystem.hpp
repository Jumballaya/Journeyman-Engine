#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Archive.hpp"

class FileSystem {
 public:
  FileSystem();
  ~FileSystem() = default;

  FileSystem(const FileSystem&) = delete;
  FileSystem& operator=(const FileSystem&) = delete;
  FileSystem(FileSystem&&) noexcept = default;
  FileSystem& operator=(FileSystem&&) noexcept = default;

  bool exists(const std::filesystem::path& filePath) const;
  std::vector<uint8_t> read(const std::filesystem::path& filePath) const;

  void mountFolder(const std::filesystem::path& folderPath);    // Starting from a folder
  void mountArchive(const std::filesystem::path& archivePath);  // Starting from a .jm archive

  // An archive entry's type; nullopt in folder mode (dispatch uses the extension).
  std::optional<std::string> typeOf(const std::filesystem::path& assetPath) const;
  std::optional<nlohmann::json> metadataOf(const std::filesystem::path& assetPath) const;

 private:
  struct FolderBackend {
    std::filesystem::path mountedFolder;
  };
  struct ArchiveBackend {
    Archive archive;
  };

  std::variant<FolderBackend, ArchiveBackend> _backend;
};
