#pragma once

#include "ModuleRegistry.hpp"

#define REGISTER_MODULE(MODNAME)                                       \
  namespace {                                                          \
  struct MODNAME##ModuleRegister {                                     \
    MODNAME##ModuleRegister() {                                        \
      GetModuleRegistry().registerModule(std::make_unique<MODNAME>()); \
    }                                                                  \
  } MODNAME##ModuleRegisterInstance;                                   \
  }