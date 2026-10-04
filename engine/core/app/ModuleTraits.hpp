#pragma once

#include "../ecs/system/TypeList.hpp"

// Specialize to order initialization: Provides/DependsOn are TypeLists of tags
// (ModuleTags.hpp). Modules without cross-module dependencies needn't specialize.
template <typename T>
struct ModuleTraits {
  using Provides = TypeList<>;
  using DependsOn = TypeList<>;
};
