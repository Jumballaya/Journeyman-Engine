#pragma once

#include <unordered_map>
#include <utility>

#include "AssetHandle.hpp"

// A module's decoded assets, keyed by the AssetHandle of their raw bytes; its
// asset converter inserts, everyone else calls get(handle).
template <typename T>
class AssetRegistry {
 public:
  void insert(AssetHandle h, T value) { _items.insert_or_assign(h, std::move(value)); }

  const T* get(AssetHandle h) const {
    auto it = _items.find(h);
    return it == _items.end() ? nullptr : &it->second;
  }

  T* get(AssetHandle h) {
    auto it = _items.find(h);
    return it == _items.end() ? nullptr : &it->second;
  }

  bool contains(AssetHandle h) const { return _items.count(h) > 0; }

  void erase(AssetHandle h) { _items.erase(h); }

  void clear() { _items.clear(); }

  size_t size() const noexcept { return _items.size(); }

 private:
  std::unordered_map<AssetHandle, T> _items;
};
