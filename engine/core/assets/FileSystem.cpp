#include "FileSystem.hpp"

#include <fstream>
#include <stdexcept>
#include <type_traits>

#include "../logger/logging.hpp"

namespace {

// Canonicalize a manifest-root-relative path to the same string form the
// archive resolver was keyed by. Mirrors AssetManager::canonicalPathKey so
// folder-mode and archive-mode behave identically for path lookups.
std::string canonicalKey(const std::filesystem::path& p) {
  return p.lexically_normal().generic_string();
}

}  // namespace

FileSystem::FileSystem() : _backend(FolderBackend{"."}) {}

bool FileSystem::exists(const std::filesystem::path& filePath) const {
  return std::visit(
      [&](const auto& backend) -> bool {
        using T = std::decay_t<decltype(backend)>;
        if constexpr (std::is_same_v<T, FolderBackend>) {
          return std::filesystem::exists(backend.mountedFolder / filePath);
        } else {
          return backend.archive.contains(canonicalKey(filePath));
        }
      },
      _backend);
}

std::vector<uint8_t> FileSystem::read(const std::filesystem::path& filePath) const {
  return std::visit(
      [&](const auto& backend) -> std::vector<uint8_t> {
        using T = std::decay_t<decltype(backend)>;
        if constexpr (std::is_same_v<T, FolderBackend>) {
          std::filesystem::path fullPath = backend.mountedFolder / filePath;

          if (!std::filesystem::exists(fullPath)) {
            JM_LOG_ERROR("File not found: {}", fullPath.string());
            throw std::runtime_error("File not found: " + fullPath.string());
          }

          std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
          if (!file.is_open()) {
            JM_LOG_ERROR("Failed to open file: {}", fullPath.string());
            throw std::runtime_error("Failed to open file: " + fullPath.string());
          }

          std::streamsize size = file.tellg();
          file.seekg(0, std::ios::beg);
          std::vector<uint8_t> buffer(size);
          if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            JM_LOG_ERROR("Failed to read file: {}", fullPath.string());
            throw std::runtime_error("Failed to read file: " + fullPath.string());
          }
          return buffer;
        } else {
          return backend.archive.read(canonicalKey(filePath)).data;
        }
      },
      _backend);
}

void FileSystem::mountFolder(const std::filesystem::path& folderPath) {
  _backend = FolderBackend{folderPath};
}

void FileSystem::mountArchive(const std::filesystem::path& archivePath) {
  _backend = ArchiveBackend{Archive::openFile(archivePath)};
}

std::optional<std::string> FileSystem::typeOf(const std::filesystem::path& assetPath) const {
  return std::visit(
      [&](const auto& backend) -> std::optional<std::string> {
        using T = std::decay_t<decltype(backend)>;
        if constexpr (std::is_same_v<T, FolderBackend>) {
          return std::nullopt;
        } else {
          auto t = backend.archive.typeOf(canonicalKey(assetPath));
          if (!t.has_value()) return std::nullopt;
          return std::string(*t);
        }
      },
      _backend);
}

std::optional<nlohmann::json> FileSystem::metadataOf(const std::filesystem::path& assetPath) const {
  return std::visit(
      [&](const auto& backend) -> std::optional<nlohmann::json> {
        using T = std::decay_t<decltype(backend)>;
        if constexpr (std::is_same_v<T, FolderBackend>) {
          return std::nullopt;
        } else {
          return backend.archive.metadataOf(canonicalKey(assetPath));
        }
      },
      _backend);
}
