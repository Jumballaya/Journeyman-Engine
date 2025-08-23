#include "FileSystem.hpp"

#include <fstream>
#include <stdexcept>

#include "../logger/logging.hpp"

bool FileSystem::exists(const std::filesystem::path& filePath) const {
  std::filesystem::path fullPath = _mountedFolder / filePath;
  return std::filesystem::exists(fullPath);
}

std::vector<uint8_t> FileSystem::read(const std::filesystem::path& filePath) const {
  std::filesystem::path fullPath = _mountedFolder / filePath;

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
}

void FileSystem::mountFolder(const std::filesystem::path& folderPath) {
  _mountedFolder = folderPath;
}

void FileSystem::mountArchive(const std::filesystem::path& /* archivePath */) {
  JM_LOG_ERROR("Archive mounting not implemented yet.");
  throw std::runtime_error("Archive mounting not implemented yet.");
}