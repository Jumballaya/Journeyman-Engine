#include "FileSystem.hpp"

#include <fstream>
#include <stdexcept>

bool FileSystem::exists(const std::filesystem::path& filePath) const {
  std::filesystem::path fullPath = _mountedFolder / filePath;
  return std::filesystem::exists(fullPath);
}

std::vector<uint8_t> FileSystem::read(const std::filesystem::path& filePath) const {
  std::filesystem::path fullPath = _mountedFolder / filePath;

  if (!std::filesystem::exists(fullPath)) {
    throw std::runtime_error("File not found: " + fullPath.string());
  }

  std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + fullPath.string());
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(size);
  if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
    throw std::runtime_error("Failed to read file: " + fullPath.string());
  }
  return buffer;
}

void FileSystem::mountFolder(const std::filesystem::path& folderPath) {
  _mountedFolder = folderPath;
}

void FileSystem::mountArchive(const std::filesystem::path& /* archivePath */) {
  throw std::runtime_error("Archive mounting not implemented yet.");
}