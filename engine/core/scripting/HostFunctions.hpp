#pragma once

#include <wasm3.h>

#include <iostream>
#include <string>

#include "../app/Application.hpp"

extern std::vector<HostFunction> coreHostFunctions;

void clearHostContext();
void setHostContext(Application& app);
