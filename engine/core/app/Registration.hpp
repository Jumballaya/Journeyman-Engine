#pragma once

#include "ModuleRegistry.hpp"

// Adds MODNAME to the module catalog at static init; every Engine registers its
// own instance. Its ModuleTraits specialization must be visible before this line.
#define REGISTER_MODULE(MODNAME)                                                                   \
  namespace {                                                                                      \
  struct MODNAME##ModuleRegister {                                                                 \
    MODNAME##ModuleRegister() {                                                                    \
      ModuleCatalog().push_back([](ModuleRegistry& registry) { registry.registerModule<MODNAME>(); }); \
    }                                                                                              \
  } MODNAME##ModuleRegisterInstance;                                                               \
  }
