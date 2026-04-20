#pragma once

#include <bitset>
#include <cstddef>
#include <functional>

struct ArchetypeSignature {
  static constexpr size_t kMaxComponents = 128;

  std::bitset<kMaxComponents> bits;

  bool isSupersetOf(const ArchetypeSignature &other) const {
    return (bits & other.bits) == other.bits;
  }

  bool operator==(const ArchetypeSignature &other) const {
    return bits == other.bits;
  }

  bool operator!=(const ArchetypeSignature &other) const {
    return !(*this == other);
  }
};

namespace std {
template <> struct hash<ArchetypeSignature> {
  size_t operator()(const ArchetypeSignature &sig) const noexcept {
    return std::hash<std::bitset<ArchetypeSignature::kMaxComponents>>{}(
        sig.bits);
  }
};
} // namespace std
