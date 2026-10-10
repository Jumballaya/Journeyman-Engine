#include "FileSystem.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "../logger/logging.hpp"

namespace {

[[noreturn]] void fail(const std::string& reason, const std::filesystem::path& path) {
  JM_LOG_ERROR("{}: {}", reason, path.string());
  throw std::runtime_error(reason + ": " + path.string());
}

}  // namespace

bool FileSystem::exists(const std::filesystem::path& filePath) const {
  return _archive ? _archive->contains(key(filePath)) : std::filesystem::exists(_folder / filePath);
}

std::optional<std::vector<uint8_t>> FileSystem::tryRead(const std::filesystem::path& filePath) const {
  if (_archive || !std::filesystem::is_regular_file(_folder / filePath)) return std::nullopt;
  std::ifstream file(_folder / filePath, std::ios::binary);
  if (!file) return std::nullopt;
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  if (file.bad()) return std::nullopt;
  return bytes;
}

std::optional<std::filesystem::file_time_type> FileSystem::modified(const std::filesystem::path& filePath) const {
  if (_archive) return std::nullopt;
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(_folder / filePath, ec);
  return ec ? std::nullopt : std::optional(time);
}

std::vector<uint8_t> FileSystem::read(const std::filesystem::path& filePath) const {
  if (_archive) return _archive->read(key(filePath)).data;

  // A directory opens as a stream too, with no size to read.
  const std::filesystem::path fullPath = _folder / filePath;
  if (!std::filesystem::is_regular_file(fullPath)) fail("File not found", fullPath);
  std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
  const std::streamsize size = file.tellg();
  std::vector<uint8_t> buffer(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
  file.seekg(0, std::ios::beg);
  if (size < 0 || !file.read(reinterpret_cast<char*>(buffer.data()), size)) fail("Failed to read file", fullPath);
  return buffer;
}

void FileSystem::mountFolder(const std::filesystem::path& folderPath) {
  _folder = folderPath;
  _archive.reset();
}

void FileSystem::mountArchive(const std::filesystem::path& archivePath) { _archive = Archive::openFile(archivePath); }

std::optional<std::string> FileSystem::typeOf(const std::filesystem::path& assetPath) const {
  if (!_archive) return std::nullopt;
  auto type = _archive->typeOf(key(assetPath));
  return type ? std::optional<std::string>(*type) : std::nullopt;
}

std::optional<nlohmann::json> FileSystem::metadataOf(const std::filesystem::path& assetPath) const {
  return _archive ? _archive->metadataOf(key(assetPath)) : std::nullopt;
}
