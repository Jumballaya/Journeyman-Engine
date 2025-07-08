#pragma once

#include <wasm3.h>

#include <iostream>
#include <string>

#include "../app/Application.hpp"

m3ApiRawFunction(abort);
m3ApiRawFunction(log);

void clearHostContext();
void setHostContext(Application& app);
