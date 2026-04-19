#pragma once

#include "ModuleRegistry.hpp"

// Registers a module via the templated register overload so its ModuleTraits
// (Provides/DependsOn tag typelists) are captured at static-init time. Any
// ModuleTraits specialization for MODNAME must be visible in the same TU as
// this macro call (e.g., earlier in the .cpp).
#define REGISTER_MODULE(MODNAME)                          \
  namespace {                                             \
  struct MODNAME##ModuleRegister {                        \
    MODNAME##ModuleRegister() {                           \
      GetModuleRegistry().registerModule<MODNAME>();      \
    }                                                     \
  } MODNAME##ModuleRegisterInstance;                      \
  }
