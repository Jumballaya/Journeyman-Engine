#pragma once

#include <wasm3.h>

#include <vector>

struct HostFunction {
  const char* module;
  const char* name;
  const char* signature;
  M3RawCall function;
};