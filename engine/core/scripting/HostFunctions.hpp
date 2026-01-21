#pragma once

#include <wasm3.h>

#include <iostream>
#include <string>

#include "../app/Application.hpp"

m3ApiRawFunction(abort);
m3ApiRawFunction(log);

void clearHostContext();
void setHostContext(Application& app);

// Helper to get current application (for use by other host function files)
Application* getCurrentApp();
