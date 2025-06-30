#pragma once

#include <wasm3.h>

#include <iostream>
#include <string>

void linkCommonHostFunctions(IM3Module module, Application& app);
void clearHostContext();