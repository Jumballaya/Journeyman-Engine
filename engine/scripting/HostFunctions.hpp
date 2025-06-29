#pragma once

#include <wasm3.h>

#include <iostream>
#include <string>

// Host function: log
m3ApiRawFunction(log);

// You can later add more host functions here
void linkCommonHostFunctions(IM3Module module, IM3Runtime runtime);