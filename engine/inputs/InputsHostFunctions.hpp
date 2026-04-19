#pragma once

#include <wasm3.h>

#include "../core/app/Engine.hpp"
#include "InputsModule.hpp"

void setInputsHostContext(Engine&, InputsModule&);
void clearInputsHostContext();

m3ApiRawFunction(jmKeyIsPressed);
m3ApiRawFunction(jmKeyIsReleased);
m3ApiRawFunction(jmKeyIsDown);