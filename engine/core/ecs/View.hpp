#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

#include "archetype/Archetype.hpp"
#include "archetype/ArchetypeSet.hpp"
#include "archetype/ArchetypeSignature.hpp"
#include "component/ComponentRegistry.hpp"
#include "entity/EntityId.hpp"

template <typename... Ts>
class View {
  static constexpr size_t kN = sizeof...(Ts);
  using IndexArray = std::array<size_t, kN>;

  class Iterator {
   public:
    Iterator(const std::vector<Archetype*>* matching, size_t archIdx, uint32_t row, const IndexArray* bits)
        : _matching(matching), _archIdx(archIdx), _row(row), _bits(bits) {
      skipEmpty();
    }

    Iterator& operator++() {
      ++_row;
      skipEmpty();
      return *this;
    }

    auto operator->() const = delete;

    std::tuple<EntityId, Ts*...> operator*() const {
      return deref(std::index_sequence_for<Ts...>{});
    }

    bool operator==(const Iterator& other) const {
      return _archIdx == other._archIdx && _row == other._row;
    }

    bool operator!=(const Iterator& other) const {
      return !(*this == other);
    }

   private:
    void skipEmpty() {
      while (_archIdx < _matching->size() && _row >= (*_matching)[_archIdx]->count()) {
        ++_archIdx;
        _row = 0;
      }
    }

    template <std::size_t... Is>
    std::tuple<EntityId, Ts*...> deref(std::index_sequence<Is...>) const {
      Archetype& arch = *(*_matching)[_archIdx];
      EntityId id = arch.entityAt(_row);
      return std::make_tuple(id, static_cast<Ts*>(arch.columnAt((*_bits)[Is], _row))...);
    }

    const std::vector<Archetype*>* _matching;
    size_t _archIdx;
    uint32_t _row;
    const IndexArray* _bits;
  };

 public:
  View(ArchetypeSet& archetypes, const ComponentRegistry& registry)
      : _bits{registry.getInfo(Ts::typeId())->bitIndex...} {
    ArchetypeSignature targetSig;
    for (size_t bit : _bits) {
      targetSig.bits.set(bit);
    }
    archetypes.forEach([&](Archetype& arch) {
      if (arch.count() == 0) return;
      if (!arch.signature().isSupersetOf(targetSig)) return;
      _matching.push_back(&arch);
    });
  }

  ~View() = default;
  View(const View&) = delete;
  View& operator=(const View&) = delete;
  View(View&&) = default;
  View& operator=(View&&) = default;

  Iterator begin() { return Iterator{&_matching, 0, 0, &_bits}; }
  Iterator end() { return Iterator{&_matching, _matching.size(), 0, &_bits}; }

 private:
  std::vector<Archetype*> _matching;
  IndexArray _bits{};
};
