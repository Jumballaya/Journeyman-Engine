#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// RAII scratch directory under the OS temp dir. Removed (recursively) when the
// object goes out of scope. Used by asset tests to stage fixture files without
// committing binary test data to the repo.
class TempDir {
 public:
  TempDir() {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string name = "jm_test_" + std::to_string(now) + "_" +
                       std::to_string(counter.fetch_add(1));
    _path = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(_path);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(_path, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::filesystem::path& path() const noexcept { return _path; }

  void writeFile(const std::filesystem::path& rel, std::string_view contents) const {
    auto full = _path / rel;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream out(full, std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }

  void writeFile(const std::filesystem::path& rel, const std::vector<uint8_t>& bytes) const {
    auto full = _path / rel;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream out(full, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }

 private:
  std::filesystem::path _path;
};
