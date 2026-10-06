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

// Iterates (EntityId, Ts*...) over every entity holding all of Ts.
template <typename... Ts>
class View {
  using IndexArray = std::array<size_t, sizeof...(Ts)>;

  class Iterator {
   public:
    Iterator(const std::vector<Archetype*>* matching, size_t archIdx, const IndexArray* bits)
        : _matching(matching), _archIdx(archIdx), _bits(bits) {
      skipEmpty();
    }

    Iterator& operator++() {
      ++_row;
      skipEmpty();
      return *this;
    }

    std::tuple<EntityId, Ts*...> operator*() const { return deref(std::index_sequence_for<Ts...>{}); }

    bool operator==(const Iterator& other) const { return _archIdx == other._archIdx && _row == other._row; }

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
      return {arch.entityAt(_row), static_cast<Ts*>(arch.columnAt((*_bits)[Is], _row))...};
    }

    const std::vector<Archetype*>* _matching;
    size_t _archIdx;
    uint32_t _row = 0;
    const IndexArray* _bits;
  };

 public:
  // A view over a component nobody registered is empty.
  View(ArchetypeSet& archetypes, const ComponentRegistry& registry) {
    const std::array<const ComponentInfo*, sizeof...(Ts)> infos{registry.getInfo(Ts::typeId())...};
    ArchetypeSignature required;
    for (size_t i = 0; i < infos.size(); ++i) {
      if (!infos[i]) return;
      _bits[i] = infos[i]->bitIndex;
      required.bits.set(_bits[i]);
    }
    archetypes.forEach([&](Archetype& arch) {
      if (arch.count() != 0 && arch.signature().isSupersetOf(required)) _matching.push_back(&arch);
    });
  }

  View(const View&) = delete;
  View& operator=(const View&) = delete;
  View(View&&) = default;
  View& operator=(View&&) = default;

  Iterator begin() { return Iterator{&_matching, 0, &_bits}; }
  Iterator end() { return Iterator{&_matching, _matching.size(), &_bits}; }

 private:
  std::vector<Archetype*> _matching;
  IndexArray _bits{};
};
