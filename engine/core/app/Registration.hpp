#pragma once

#include "ModuleRegistry.hpp"

// Registers MODNAME at static init; its ModuleTraits specialization must be
// visible before this line.
#define REGISTER_MODULE(MODNAME)                          \
  namespace {                                             \
  struct MODNAME##ModuleRegister {                        \
    MODNAME##ModuleRegister() {                           \
      GetModuleRegistry().registerModule<MODNAME>();      \
    }                                                     \
  } MODNAME##ModuleRegisterInstance;                      \
  }
