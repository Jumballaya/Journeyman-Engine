#pragma once

#include "../ecs/system/TypeList.hpp"

// Modules declare their dependency profile by specializing ModuleTraits for
// their type. Provides is a TypeList of tag types the module satisfies;
// DependsOn is a TypeList of tag types the module needs before it initializes.
// ModuleRegistry topologically sorts modules based on these before running
// initialize().
//
// Example:
//   struct WindowTag {};
//   template <> struct ModuleTraits<GLFWWindowModule> {
//     using Provides  = TypeList<WindowTag>;
//     using DependsOn = TypeList<>;
//   };
//   template <> struct ModuleTraits<InputsModule> {
//     using Provides  = TypeList<>;
//     using DependsOn = TypeList<WindowTag>;
//   };
//
// Modules without cross-module dependencies don't need to specialize.
template <typename T>
struct ModuleTraits {
  using Provides = TypeList<>;
  using DependsOn = TypeList<>;
};
