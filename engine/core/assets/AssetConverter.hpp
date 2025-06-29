#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "AssetHandle.hpp"
#include "RawAsset.hpp"

using ConverterCallback = std::function<void(const RawAsset&, const AssetHandle&)>;

struct FileExtensionHandle {
  uint32_t id;

  constexpr bool operator==(const FileExtensionHandle& other) const noexcept { return id == other.id; }
  constexpr bool operator!=(const FileExtensionHandle& other) const noexcept { return id != other.id; }
};

struct ConverterCallbackHandle {
  uint32_t id;

  constexpr bool operator==(const ConverterCallbackHandle& other) const noexcept { return id == other.id; }
};

namespace std {
template <>
struct hash<FileExtensionHandle> {
  size_t operator()(const FileExtensionHandle& handle) const noexcept {
    return std::hash<uint32_t>{}(handle.id);
  }
};

template <>
struct hash<ConverterCallbackHandle> {
  size_t operator()(const ConverterCallbackHandle& handle) const noexcept {
    return std::hash<uint32_t>{}(handle.id);
  }
};
}  // namespace std